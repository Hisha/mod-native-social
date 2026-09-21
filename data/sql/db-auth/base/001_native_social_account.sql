-- Native Social: account-level profiles (auth database).
-- Rerunnable; safe to let the AzerothCore updater apply on every startup.
-- Bound by the module's display-name limits: the max length accepted by the
-- server is 48 display characters, matching the column width below.

CREATE TABLE IF NOT EXISTS `native_social_account` (
    `account_id` INT UNSIGNED NOT NULL,
    `display_name` VARCHAR(48) CHARACTER SET utf8mb4 COLLATE utf8mb4_unicode_ci NOT NULL,
    `display_name_key` VARCHAR(48) CHARACTER SET utf8mb4 COLLATE utf8mb4_bin NOT NULL,
    `appear_offline` TINYINT(1) UNSIGNED NOT NULL DEFAULT 0,
    `created_at` TIMESTAMP NOT NULL DEFAULT CURRENT_TIMESTAMP,
    `updated_at` TIMESTAMP NOT NULL DEFAULT CURRENT_TIMESTAMP ON UPDATE CURRENT_TIMESTAMP,
    PRIMARY KEY (`account_id`),
    UNIQUE KEY `uq_native_social_display_name_key` (`display_name_key`)
) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4;