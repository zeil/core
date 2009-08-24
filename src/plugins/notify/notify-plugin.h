#ifndef NOTIFY_PLUGIN_H
#define NOTIFY_PLUGIN_H

enum mail_flags;
struct mail;
struct mail_transaction_commit_changes;
struct mail_storage;
struct mailbox_transaction_context;
struct mailbox_list;
struct notify_context;

struct notify_vfuncs {
	void *(*mail_transaction_begin)(struct mailbox_transaction_context *t);
	void (*mail_save)(void *txn, struct mail *mail);
	void (*mail_copy)(void *txn, struct mail *src, struct mail *dst);
	void (*mail_expunge)(void *txn, struct mail *mail);
	void (*mail_update_flags)(void *txn, struct mail *mail,
				  enum mail_flags old_flags);
	void (*mail_update_keywords)(void *txn, struct mail *mail,
				     const char *const *old_keywords);
	void (*mail_transaction_commit)(void *txn,
			struct mail_transaction_commit_changes *changes);
	void (*mail_transaction_rollback)(void *txn);
	void *(*mailbox_delete_begin)(struct mailbox_list *list, 
				      const char *name);
	void (*mailbox_delete_commit)(void *txn, struct mailbox_list *list,
				      const char *name);
	void (*mailbox_delete_rollback)(void *txn);
	void (*mailbox_rename)(struct mailbox_list *oldlist,
			       const char *oldname,
			       struct mailbox_list *newlist,
			       const char *newname, bool rename_children);
};

void notify_noop_mail_transaction_begin(struct mailbox_transaction_context *t);
void notify_noop_mail_save(void *txn, struct mail *mail);
void notify_noop_mail_copy(void *txn, struct mail *src, struct mail *dst);
void notify_noop_mail_expunge(void *txn, struct mail *mail);
void notify_noop_mail_update_flags(void *txn, struct mail *mail,
				   enum mail_flags old_flags);
void notify_noop_mail_update_keywords(void *txn, struct mail *mail,
				      const char *const *old_keywords);
void notify_noop_mail_transaction_commit(void *txn,
					 struct mail_transaction_commit_changes *changes);
void notify_noop_mail_transaction_rollback(void *txn);
void *notify_noop_mailbox_delete_begin(struct mailbox_list *list,
				       const char *name);
void notify_noop_mailbox_delete_commit(void *txn, struct mailbox_list *list,
				       const char *name);
void notify_noop_mailbox_delete_rollback(void *txn);
void notify_noop_mailbox_rename(struct mailbox_list *oldlist,
				const char *oldname,
				struct mailbox_list *newlist,
				const char *newname, bool rename_children);

struct notify_context *
notify_register(const struct notify_vfuncs *vfuncs);
void notify_unregister(struct notify_context *ctx);

void notify_plugin_init(void);
void notify_plugin_deinit(void);

#endif
