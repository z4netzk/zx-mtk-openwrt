include $(TOPDIR)/rules.mk

PKG_NAME:=libwtinfo
PKG_RELEASE:=1

include $(INCLUDE_DIR)/package.mk

define Package/$(PKG_NAME)
  SECTION:=WT
  CATEGORY:=WT
  SUBMENU:=libs
  DEPENDS:=+libopenssl +cJSON
  TITLE:=WT board info lib
endef

define Package/libwtinfo-lua
  SECTION:=WT
  CATEGORY:=WT
  SUBMENU:=libs
  DEPENDS:=+libwtinfo +liblua +cJSON
  TITLE:=WT wtinfo lua lib
endef

define Package/wtinfo
  SECTION:=WT
  CATEGORY:=WT
  TITLE:=WT board info client
  DEPENDS:=+libwtinfo +libopenssl +cJSON
  SUBMENU:=utils
endef

define Package/wtinfo/description
Command line frontend for the wtinfo information library.
endef

define Package/libwtinfo-lua/description
 WT info lua lib.
endef

define Package/$(PKG_NAME)/description
 WT wtinfo lib.
endef

TARGET_CFLAGS += -D_GNU_SOURCE

define Build/Prepare
	mkdir -p $(PKG_BUILD_DIR)
	$(CP) ./src/* $(PKG_BUILD_DIR)/
endef

define Build/InstallDev
	$(INSTALL_DIR) $(1)/usr/include
	$(CP) $(PKG_BUILD_DIR)/wtinfo.h $(1)/usr/include/
	$(INSTALL_DIR) $(1)/usr/lib
	$(INSTALL_BIN) $(PKG_BUILD_DIR)/libwtinfo.so $(1)/usr/lib/libwtinfo.so
endef

define Package/$(PKG_NAME)/install
	$(INSTALL_DIR) $(1)/usr/lib
	$(INSTALL_DATA) $(PKG_BUILD_DIR)/libwtinfo.so $(1)/usr/lib/libwtinfo.so
endef

define Package/libwtinfo-lua/install
	$(INSTALL_DIR) $(1)/usr/lib/lua
	$(INSTALL_DATA) $(PKG_BUILD_DIR)/wtinfo.so $(1)/usr/lib/lua/
endef

define Package/wtinfo/install
	$(INSTALL_DIR) $(1)/usr/bin $(1)/lib/functions
	$(INSTALL_BIN) $(PKG_BUILD_DIR)/wtinfo $(1)/usr/bin/wtinfo
	$(INSTALL_BIN) ./files/wtinfo.sh $(1)/lib/functions/wtinfo.sh
endef

$(eval $(call BuildPackage,$(PKG_NAME)))
$(eval $(call BuildPackage,libwtinfo-lua))
$(eval $(call BuildPackage,wtinfo))
