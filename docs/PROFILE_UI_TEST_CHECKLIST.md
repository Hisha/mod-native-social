# Native Social profile UI test checklist

Use a client build produced from the updated schema-3 EPF. Run the normal and
administrator cases with separate accounts. Keep a second normal account
online on another client so directory visibility can be observed independently.

## Normal player

1. Log in with a normal eligible account and open Social → Players. Confirm all
   six tabs render, the Directory/My Profile buttons render, Refresh still
   works, and no Admin button is shown.
2. Open My Profile. Confirm the current Display Name and Appear Offline state
   match `native_social_account`.
3. Enter a unique valid name and click Save Profile. Confirm `profile saved.`,
   return to Directory, and confirm the new name appears immediately without a
   worldserver restart.
4. Try a too-short name, a name containing `|`, and another account's existing
   Display Name. Confirm each produces an error and neither the displayed name
   nor database row changes.
5. Enable Appear Offline and save. From the second client, refresh Players and
   confirm this account is offline and exposes no character name, level, race,
   class, faction, or location. Disable it and save; confirm live presence
   returns immediately.
6. Log in two characters on the same account, enable Appear Offline from one,
   and confirm neither character leaks through the second client's directory.
7. Restart worldserver and confirm the saved name and Appear Offline state are
   restored from the auth database.
8. Craft `PROFILE_SAVE` with an extra account-ID field. Confirm the server
   returns a wrong-field-count error and the target account is unchanged.
9. Craft `ADMIN_LIST` and `ADMIN_SET_NAME` from the normal account. Confirm the
   server returns an authorization error and no profile changes.

## Administrator

1. Log in with an account at `SEC_ADMINISTRATOR`, open Social → Players, and
   confirm the Admin button appears only after the server capability response.
2. Open Admin and confirm every eligible human account ID is listed, including
   accounts showing `Not configured`; confirm excluded/Playerbot accounts and
   authentication usernames are absent.
3. Select an unconfigured account, enter a valid unique Display Name, and click
   Assign Name. Confirm success, the refreshed admin row, and immediate public
   directory membership without restarting worldserver.
4. Change an existing profile name. Confirm the old name disappears and the new
   one appears after refresh. Repeat with a duplicate and invalid name and
   confirm the server refuses both.
5. Send a crafted `ADMIN_SET_NAME` for an excluded or nonexistent account ID.
   Confirm the server refuses it.
6. Confirm the UI contains no account-name/password creation controls and emits
   no `ADMIN_CREATE_ACCOUNT` request. Account creation remains a separately
   reviewed follow-up.
