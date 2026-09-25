# Native Social profile UI test checklist

Use a client build produced from the updated schema-3 EPF. Run the normal and
administrator cases with separate accounts. Keep a second normal account
online on another client so directory visibility can be observed independently.

## Normal player

1. Log in with a normal eligible account and open Social → Players. Confirm all
   six tabs render. On Directory confirm only My Profile and Refresh appear;
   on My Profile confirm only Directory appears; confirm no Admin button or
   empty navigation gap is shown.
2. Open My Profile. Confirm the current Display Name and Appear Offline state
   match `native_social_account`.
3. Enter a unique valid name and click Save Profile. Confirm `profile saved.`,
   return to Directory, and confirm the new name appears immediately without a
   worldserver restart.
4. Edit the name without saving, navigate to Directory, and return to My
   Profile. Confirm the draft remains and the UI explains that unsaved edits
   were preserved. Save or restore the original value before continuing.
5. Try a too-short name, a name containing `|`, and another account's existing
   Display Name. Confirm each produces an error and neither the displayed name
   nor database row changes.
6. Enable Appear Offline and save. From the second client, refresh Players and
   confirm this account is offline and exposes no character name, level, race,
   class, faction, or location. Disable it and save; confirm live presence
   returns immediately.
7. Log in two characters on the same account, enable Appear Offline from one,
   and confirm neither character leaks through the second client's directory.
8. With two different eligible accounts logged in, refresh both directories.
   Confirm each sees the other account and neither sees itself. Repeat with a
   second character on either account.
9. Restart worldserver and confirm the saved name and Appear Offline state are
   restored from the auth database.
10. Craft `PROFILE_SAVE` with an extra account-ID field. Confirm the server
   returns a wrong-field-count error and the target account is unchanged.
11. Craft `ADMIN_LIST` and `ADMIN_SET_NAME` from the normal account. Confirm the
   server returns an authorization error and no profile changes.

## Administrator

1. Log in with an account at `SEC_ADMINISTRATOR`, open Social → Players, and
   confirm Directory shows My Profile, Admin, and Refresh with no Directory
   button. Confirm My Profile shows Directory and Admin; Admin shows Directory
   and My Profile. No page should show its own button.
2. Open Admin and confirm every eligible human account ID is listed, including
   accounts showing `Not configured`; confirm excluded/Playerbot accounts and
   authentication usernames are absent. Confirm the administrator's own
   account remains in this Admin list even though it is absent from Directory.
3. Select unconfigured account `206`, enter `Isaac`, and click Assign Name.
   Confirm `Display name saved` and the refreshed row shows Isaac. Refresh the
   second account's public directory and confirm Isaac appears immediately
   without restarting worldserver.
4. Change an existing profile name. Confirm the old name disappears and the new
   one appears after refresh. Repeat with a duplicate and invalid name and
   confirm the server refuses both.
5. Send a crafted `ADMIN_SET_NAME` for an excluded or nonexistent account ID.
   Confirm the server refuses it.
6. Confirm the UI contains no account-name/password creation controls and emits
   no `ADMIN_CREATE_ACCOUNT` request. Account creation remains a separately
   reviewed follow-up.
7. Restart worldserver and confirm account 206 still has Display Name Isaac.
