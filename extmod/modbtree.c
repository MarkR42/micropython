/*
 * This file is part of the MicroPython project, http://micropython.org/
 *
 * The MIT License (MIT)
 *
 * Copyright (c) 2016 Paul Sokolovsky
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and associated documentation files (the "Software"), to deal
 * in the Software without restriction, including without limitation the rights
 * to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
 * copies of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 * AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN
 * THE SOFTWARE.
 */

#include <stdio.h>
#include <string.h>
#include <errno.h>
#include <fcntl.h>

#include "py/nlr.h"
#include "py/runtime.h"

#if MICROPY_PY_BTREE

#include <db.h>
#include <../../btree/btree.h>

typedef struct _mp_obj_btree_t {
    mp_obj_base_t base;
    DB *db;
    mp_obj_t start_key;
    mp_obj_t end_key;
    bool end_key_inclusive;
} mp_obj_btree_t;

STATIC const mp_obj_type_t btree_type;

#define CHECK_ERROR(res) \
        if (res == RET_ERROR) { \
            nlr_raise(mp_obj_new_exception_arg1(&mp_type_OSError, MP_OBJ_NEW_SMALL_INT(errno))); \
        }

void __dbpanic(DB *db) {
    printf("__dbpanic(%p)\n", db);
}

STATIC mp_obj_btree_t *btree_new(DB *db) {
    mp_obj_btree_t *o = m_new_obj(mp_obj_btree_t);
    o->base.type = &btree_type;
    o->db = db;
    o->start_key = MP_OBJ_NULL;
    o->end_key = MP_OBJ_NULL;
    return o;
}

STATIC void btree_print(const mp_print_t *print, mp_obj_t self_in, mp_print_kind_t kind) {
    (void)kind;
    mp_obj_btree_t *self = MP_OBJ_TO_PTR(self_in);
    mp_printf(print, "<btree %p>", self->db);
}

STATIC mp_obj_t btree_close(mp_obj_t self_in) {
    mp_obj_btree_t *self = MP_OBJ_TO_PTR(self_in);
    return MP_OBJ_NEW_SMALL_INT(__bt_close(self->db));
}
STATIC MP_DEFINE_CONST_FUN_OBJ_1(btree_close_obj, btree_close);

STATIC mp_obj_t btree_put(size_t n_args, const mp_obj_t *args) {
    mp_obj_btree_t *self = MP_OBJ_TO_PTR(args[0]);
    DBT key, val;
    key.data = (void*)mp_obj_str_get_data(args[1], &key.size);
    val.data = (void*)mp_obj_str_get_data(args[2], &val.size);
    return MP_OBJ_NEW_SMALL_INT(__bt_put(self->db, &key, &val, 0));
}
STATIC MP_DEFINE_CONST_FUN_OBJ_VAR_BETWEEN(btree_put_obj, 3, 4, btree_put);

STATIC mp_obj_t btree_get(size_t n_args, const mp_obj_t *args) {
    mp_obj_btree_t *self = MP_OBJ_TO_PTR(args[0]);
    DBT key, val;
    key.data = (void*)mp_obj_str_get_data(args[1], &key.size);
    int res = __bt_get(self->db, &key, &val, 0);
    if (res == RET_SPECIAL) {
        if (n_args > 2) {
            return args[2];
        } else {
            return mp_const_none;
        }
    }
    CHECK_ERROR(res);
    return mp_obj_new_bytes(val.data, val.size);
}
STATIC MP_DEFINE_CONST_FUN_OBJ_VAR_BETWEEN(btree_get_obj, 2, 3, btree_get);

STATIC mp_obj_t btree_seq(size_t n_args, const mp_obj_t *args) {
    mp_obj_btree_t *self = MP_OBJ_TO_PTR(args[0]);
    int flags = MP_OBJ_SMALL_INT_VALUE(args[1]);
    DBT key, val;
    if (n_args > 2) {
        key.data = (void*)mp_obj_str_get_data(args[2], &key.size);
    }

    int res = __bt_seq(self->db, &key, &val, flags);
    if (res == RET_SPECIAL) {
        return mp_const_none;
    }

    mp_obj_tuple_t *pair = mp_obj_new_tuple(2, NULL);
    pair->items[0] = mp_obj_new_bytes(key.data, key.size);
    pair->items[1] = mp_obj_new_bytes(val.data, val.size);
    return pair;
}
STATIC MP_DEFINE_CONST_FUN_OBJ_VAR_BETWEEN(btree_seq_obj, 2, 4, btree_seq);

STATIC mp_obj_t btree_range(size_t n_args, const mp_obj_t *args) {
    mp_obj_btree_t *self = MP_OBJ_TO_PTR(args[0]);
    self->start_key = args[1];
    self->end_key = args[2];
    self->end_key_inclusive = false;
    return args[0];
}
STATIC MP_DEFINE_CONST_FUN_OBJ_VAR_BETWEEN(btree_range_obj, 3, 4, btree_range);

STATIC mp_obj_t btree_iternext(mp_obj_t self_in) {
    mp_obj_btree_t *self = MP_OBJ_TO_PTR(self_in);
    DBT key, val;
    int res;
    if (self->start_key != MP_OBJ_NULL) {
        int flags = R_FIRST;
        if (self->start_key != mp_const_none) {
            key.data = (void*)mp_obj_str_get_data(self->start_key, &key.size);
            flags = R_CURSOR;
        }
        res = __bt_seq(self->db, &key, &val, flags);
        self->start_key = MP_OBJ_NULL;
    } else {
        res = __bt_seq(self->db, &key, &val, R_NEXT);
    }

    if (res == RET_SPECIAL) {
        return MP_OBJ_STOP_ITERATION;
    }
    CHECK_ERROR(res);

    if (self->end_key != mp_const_none) {
        DBT end_key;
        end_key.data = (void*)mp_obj_str_get_data(self->end_key, &end_key.size);
        BTREE *t = self->db->internal;
        if (t->bt_cmp(&key, &end_key) >= 0) {
            self->end_key = MP_OBJ_NULL;
            return MP_OBJ_STOP_ITERATION;
        }
    }

    mp_obj_tuple_t *pair = mp_obj_new_tuple(2, NULL);
    pair->items[0] = mp_obj_new_bytes(key.data, key.size);
    pair->items[1] = mp_obj_new_bytes(val.data, val.size);
    return pair;
}

STATIC mp_obj_t btree_subscr(mp_obj_t self_in, mp_obj_t index, mp_obj_t value) {
    mp_obj_btree_t *self = MP_OBJ_TO_PTR(self_in);
    if (value == MP_OBJ_NULL) {
        // delete
        DBT key;
        key.data = (void*)mp_obj_str_get_data(index, &key.size);
        int res = __bt_delete(self->db, &key, 0);
        if (res == RET_SPECIAL) {
            nlr_raise(mp_obj_new_exception(&mp_type_KeyError));
        }
        CHECK_ERROR(res);
        return mp_const_none;
    } else if (value == MP_OBJ_SENTINEL) {
        // load
        DBT key, val;
        key.data = (void*)mp_obj_str_get_data(index, &key.size);
        int res = __bt_get(self->db, &key, &val, 0);
        if (res == RET_SPECIAL) {
            return mp_const_none;
        }
        CHECK_ERROR(res);
        return mp_obj_new_bytes(val.data, val.size);
    } else {
        // store
        DBT key, val;
        key.data = (void*)mp_obj_str_get_data(index, &key.size);
        val.data = (void*)mp_obj_str_get_data(value, &val.size);
        int res = __bt_put(self->db, &key, &val, 0);
        CHECK_ERROR(res);
        return mp_const_none;
    }
}

STATIC const mp_rom_map_elem_t btree_locals_dict_table[] = {
    { MP_ROM_QSTR(MP_QSTR_close), MP_ROM_PTR(&btree_close_obj) },
    { MP_ROM_QSTR(MP_QSTR_get), MP_ROM_PTR(&btree_get_obj) },
    { MP_ROM_QSTR(MP_QSTR_put), MP_ROM_PTR(&btree_put_obj) },
    { MP_ROM_QSTR(MP_QSTR_seq), MP_ROM_PTR(&btree_seq_obj) },
    { MP_ROM_QSTR(MP_QSTR_items), MP_ROM_PTR(&btree_range_obj) },
};

STATIC MP_DEFINE_CONST_DICT(btree_locals_dict, btree_locals_dict_table);

STATIC const mp_obj_type_t btree_type = {
    { &mp_type_type },
    // Save on qstr's, reuse same as for module
    .name = MP_QSTR_btree,
    .print = btree_print,
    .getiter = mp_identity,
    .iternext = btree_iternext,
    .subscr = btree_subscr,
    .locals_dict = (void*)&btree_locals_dict,
};

STATIC mp_obj_t mod_btree_open(size_t n_args, const mp_obj_t *pos_args, mp_map_t *kw_args) {
    static const mp_arg_t allowed_args[] = {
        { MP_QSTR_server_side, MP_ARG_KW_ONLY | MP_ARG_BOOL, {.u_bool = false} },
    };

    const char *fname = mp_obj_str_get_str(pos_args[0]);

    struct {
        mp_arg_val_t server_side;
    } args;
    mp_arg_parse_all(n_args - 1, pos_args + 1, kw_args,
        MP_ARRAY_SIZE(allowed_args), allowed_args, (mp_arg_val_t*)&args);

    DB *db = __bt_open(fname, /*flags*/O_CREAT | O_RDWR, /*mode*/0770, /*openinfo*/NULL, /*dflags*/0);
    return MP_OBJ_FROM_PTR(btree_new(db));
}
STATIC MP_DEFINE_CONST_FUN_OBJ_KW(mod_btree_open_obj, 1, mod_btree_open);

STATIC const mp_rom_map_elem_t mp_module_btree_globals_table[] = {
    { MP_ROM_QSTR(MP_QSTR___name__), MP_ROM_QSTR(MP_QSTR_btree) },
    { MP_ROM_QSTR(MP_QSTR_open), MP_ROM_PTR(&mod_btree_open_obj) },
};

STATIC MP_DEFINE_CONST_DICT(mp_module_btree_globals, mp_module_btree_globals_table);

const mp_obj_module_t mp_module_btree = {
    .base = { &mp_type_module },
    .name = MP_QSTR_btree,
    .globals = (mp_obj_dict_t*)&mp_module_btree_globals,
};

#endif // MICROPY_PY_BTREE
