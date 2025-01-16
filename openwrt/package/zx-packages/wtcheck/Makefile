
include $(TOPDIR)/rules.mk

PKG_NAME:=wtcheck
PKG_VERSION:=1.0
PKG_RELEASE:=2021.03.23

PKG_BUILD_DIR := $(BUILD_DIR)/$(PKG_NAME)_$(PKG_VERSION)
include $(INCLUDE_DIR)/package.mk

define Package/$(PKG_NAME)
  SECTION:=WT
  CATEGORY:=WT
  TITLE:=WT firmware check
  PKGARCH:=all
  SUBMENU:=utils
endef

define Package/$(PKG_NAME)/description
Provide supporting wt firmare check.
endef

define Build/Prepare
	mkdir -p $(PKG_BUILD_DIR)
	$(CP) ./src/* $(PKG_BUILD_DIR)/
endef

TARGET_CFLAGS += -D_GNU_SOURCE

define Package/$(PKG_NAME)/install
	$(INSTALL_DIR) $(1)/sbin
	$(INSTALL_BIN) $(PKG_BUILD_DIR)/wtcheck $(1)/sbin/wtcheck
endef

$(eval $(call BuildPackage,$(PKG_NAME)))
