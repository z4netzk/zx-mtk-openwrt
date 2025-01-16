#include <lua.h>
#include <lauxlib.h>
#include "wtinfo.h"

static int wtinfo_L_get_value(lua_State *L) {
  const char *key = luaL_checkstring(L, 1);

  if (key) {
    void *info;
    const char *val;

    info = wtinfo_init();

    if (info) {
      val = wtinfo_get_val(info, key);

      if (val)
        lua_pushstring(L, val);
      else
        lua_pushnil(L);

      wtinfo_deinit(info);
    } else {
      lua_pushnil(L);
    }
  } else {
    lua_pushnil(L);
  }

  return 1;
}

static const luaL_reg wtinfo_reg[] = {
  {"get", wtinfo_L_get_value},
  {NULL, NULL}
};

int luaopen_wtinfo(lua_State *L) {
  luaL_register(L, "wtinfo", wtinfo_reg);
  return 1;
}
