#
# Copyright (C) 2015 OpenWrt.org
#
# This is free software, licensed under the GNU General Public License v2.
# See /LICENSE for more information.
#

include $(TOPDIR)/rules.mk

PKG_NAME:=lua-cjson
PKG_VERSION:=2.1.0.6
PKG_RELEASE:=2
PKG_MAINTAINER:=Dirk Chang <dirk@kooiot.com>
PKG_LICENSE:=MIT
PKG_LICENSE_FILES:=LICENSE
PKG_SOURCE_PROTO:=git

#PKG_SOURCE_URL:=https://github.com/LuaDist-testing/lua-cjson2.git
#PKG_SOURCE_VERSION:=b6f0d4ea2754cea00016689cedcbe509e9091923
#PKG_SOURCE:=$(PKG_NAME)-$(PKG_VERSION)-$(PKG_SOURCE_VERSION).tar.gz
#PKG_SOURCE_SUBDIR:=$(PKG_NAME)-$(PKG_VERSION)-$(PKG_SOURCE_VERSION)
#PKG_BUILD_DIR:=$(BUILD_DIR)/$(PKG_NAME)-$(PKG_VERSION)-$(PKG_SOURCE_VERSION)
#PKG_HASH:=824b1c687233393a03470e2eed3923d877242b0d82d4c7defde968a55e46e274

HOST_BUILD_DEPENDS:=lua/host

include $(INCLUDE_DIR)/host-build.mk
include $(INCLUDE_DIR)/package.mk
include $(INCLUDE_DIR)/cmake.mk

define Package/lua-cjson
  SUBMENU:=Lua
  SECTION:=lang
  CATEGORY:=Languages
  TITLE:=Lua CJSON parser
  URL:=https://github.com/mpx/lua-cjson
  DEPENDS:= +lua
endef

define Package/lua-cjson/description
  Lua CJSON is a fast JSON encoding/parsing module for Lua.
endef

CMAKE_OPTIONS += \
	-DUSE_LUA=ON

CMAKE_HOST_OPTIONS += \
	-DLUA_MATH_LIBRARY=m

define Package/lua-cjson/install
	$(INSTALL_DIR) $(1)/usr/lib/lua
	$(INSTALL_BIN) $(PKG_BUILD_DIR)/cjson.so $(1)/usr/lib/lua/

	$(INSTALL_DIR) $(1)/usr/lib/lua/cjson
	$(INSTALL_DATA) $(PKG_BUILD_DIR)/lua/cjson/util.lua $(1)/usr/lib/lua/cjson
endef

$(eval $(call HostBuild))
$(eval $(call BuildPackage,lua-cjson))
