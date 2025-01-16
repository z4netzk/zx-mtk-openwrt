include $(TOPDIR)/rules.mk

PKG_NAME:=wtoem
PKG_RELEASE:=1

include $(INCLUDE_DIR)/package.mk

define Package/wtoem
  SECTION:=WT
  CATEGORY:=WT
  TITLE:=WT oem
  SUBMENU:=utils
endef

define Package/wtoem/description
Command line frontend for the wtinfo information library.
endef

TARGET_CFLAGS += -D_GNU_SOURCE

define Build/Prepare
	mkdir -p $(PKG_BUILD_DIR)
	$(CP) ./src/* $(PKG_BUILD_DIR)/
endef

define Package/wtoem/install
	$(INSTALL_DIR) $(1)/usr/bin $(1)/lib/functions
	$(INSTALL_BIN) $(PKG_BUILD_DIR)/wtoem $(1)/usr/bin/wtoem
endef

$(eval $(call BuildPackage,wtoem))
