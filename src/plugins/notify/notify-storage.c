#include "lib.h"
#include "array.h"
#include "mail-storage-private.h"
#include "mailbox-list-private.h"
#include "notify-plugin-private.h"

#define NOTIFY_CONTEXT(obj) \
	MODULE_CONTEXT(obj, notify_storage_module)
#define NOTIFY_MAIL_CONTEXT(obj) \
	MODULE_CONTEXT(obj, notify_mail_module)
#define NOTIFY_LIST_CONTEXT(obj) \
	MODULE_CONTEXT(obj, notify_mailbox_list_module)

struct notify_transaction_context {
	union mailbox_transaction_module_context module_ctx;
	struct mail *tmp_mail;
	bool save_hack;
};

static MODULE_CONTEXT_DEFINE_INIT(notify_storage_module,
				  &mail_storage_module_register);
static MODULE_CONTEXT_DEFINE_INIT(notify_mail_module,
				  &mail_module_register);
static MODULE_CONTEXT_DEFINE_INIT(notify_mailbox_list_module,
				  &mailbox_list_module_register);

static void (*notify_next_hook_mail_storage_created)
	(struct mail_storage *storage);
static void (*notify_next_hook_mailbox_list_created)
	(struct mailbox_list *list);

static void
notify_mail_expunge(struct mail *_mail)
{
	struct mail_private *mail = (struct mail_private *)_mail;
	union mail_module_context *lmail = NOTIFY_MAIL_CONTEXT(mail);
	
	notify_contexts_mail_expunge(_mail);
	lmail->super.expunge(_mail);
}

static void
notify_mail_update_flags(struct mail *_mail, enum modify_type modify_type,
			 enum mail_flags flags)
{
	struct mail_private *mail = (struct mail_private *)_mail;
	union mail_module_context *lmail = NOTIFY_MAIL_CONTEXT(mail);
	enum mail_flags old_flags, new_flags;

	old_flags = mail_get_flags(_mail);
	lmail->super.update_flags(_mail, modify_type, flags);
	new_flags = mail_get_flags(_mail);

	if ((old_flags ^ new_flags) == 0) 
		return;
	
	notify_contexts_mail_update_flags(_mail, old_flags);
}

static void
notify_mail_update_keywords(struct mail *_mail, enum modify_type modify_type,
			    struct mail_keywords *keywords)
{
	struct mail_private *mail = (struct mail_private *)_mail;
	union mail_module_context *lmail = NOTIFY_MAIL_CONTEXT(mail);
	const char *const *old_keywords, *const *new_keywords;
	unsigned int i;

	old_keywords = mail_get_keywords(_mail);
	lmail->super.update_keywords(_mail, modify_type, keywords);
	new_keywords = mail_get_keywords(_mail);

	for (i = 0; old_keywords[i] != NULL && new_keywords[i] != NULL; i++) {
		if (strcmp(old_keywords[i], new_keywords[i]) != 0)
			break;
	}

	if (old_keywords[i] == NULL && new_keywords[i] == NULL) 
		return;

	notify_contexts_mail_update_keywords(_mail, old_keywords);
}

static struct mail *
notify_mail_alloc(struct mailbox_transaction_context *t,
		  enum mail_fetch_field wanted_fields,
		  struct mailbox_header_lookup_ctx *wanted_headers)
{
	union mailbox_module_context *lbox = NOTIFY_CONTEXT(t->box);
	union mail_module_context *lmail;
	struct mail *_mail;
	struct mail_private *mail;

	_mail = lbox->super.mail_alloc(t, wanted_fields, wanted_headers);
	mail = (struct mail_private *)_mail;

	lmail = p_new(mail->pool, union mail_module_context, 1);
	lmail->super = mail->v;

	mail->v.expunge = notify_mail_expunge;
	mail->v.update_flags = notify_mail_update_flags;
	mail->v.update_keywords = notify_mail_update_keywords;
	MODULE_CONTEXT_SET_SELF(mail, notify_mail_module, lmail);
	return _mail;
}

static int
notify_copy(struct mail_save_context *ctx, struct mail *mail)
{
	struct notify_transaction_context *lt =
		NOTIFY_CONTEXT(ctx->transaction);
	union mailbox_module_context *lbox =
		NOTIFY_CONTEXT(ctx->transaction->box);
	int ret;

	if (ctx->dest_mail == NULL) {
		if (lt->tmp_mail == NULL)
			lt->tmp_mail = mail_alloc(ctx->transaction, 0, NULL);
		ctx->dest_mail = lt->tmp_mail;
	}

	lt->save_hack = TRUE;
	ret = lbox->super.copy(ctx, mail);
	lt->save_hack = FALSE;

	if (ret == 0)
		notify_contexts_mail_copy(mail, ctx->dest_mail);
	return ret;
}

static int
notify_save_begin(struct mail_save_context *ctx, struct istream *input)
{
	struct notify_transaction_context *lt =
		NOTIFY_CONTEXT(ctx->transaction);
	union mailbox_module_context *lbox =
		NOTIFY_CONTEXT(ctx->transaction->box);

	if (ctx->dest_mail == NULL) {
		if (lt->tmp_mail == NULL)
			lt->tmp_mail = mail_alloc(ctx->transaction, 0, NULL);
		ctx->dest_mail = lt->tmp_mail;
	}
	return lbox->super.save_begin(ctx, input);
}

static int
notify_save_finish(struct mail_save_context *ctx)
{
	struct notify_transaction_context *lt =
		NOTIFY_CONTEXT(ctx->transaction);
	union mailbox_module_context *lbox =
		NOTIFY_CONTEXT(ctx->transaction->box);

	if (lbox->super.save_finish(ctx) < 0)
		return -1;
	if (!lt->save_hack)
		notify_contexts_mail_save(ctx->dest_mail);
	return 0;
}

static struct mailbox_transaction_context *
notify_transaction_begin(struct mailbox *box,
			 enum mailbox_transaction_flags flags)
{
	union mailbox_module_context *lbox = NOTIFY_CONTEXT(box);
	struct mailbox_transaction_context *t;
	struct notify_transaction_context *lt;
	
	t = lbox->super.transaction_begin(box, flags);

	lt = i_new(struct notify_transaction_context, 1);
	MODULE_CONTEXT_SET(t, notify_storage_module, lt);

	notify_contexts_mail_transaction_begin(t);
	return t;
}

static int
notify_transaction_commit(struct mailbox_transaction_context *t,
			  struct mail_transaction_commit_changes *changes_r)
{
	struct notify_transaction_context *lt = NOTIFY_CONTEXT(t);
	union mailbox_module_context *lbox = NOTIFY_CONTEXT(t->box);

	if (lt->tmp_mail != NULL)
		mail_free(&lt->tmp_mail);
	i_free(lt);

	if ((lbox->super.transaction_commit(t, changes_r)) < 0) {
		notify_contexts_mail_transaction_rollback(t);
		return -1;
	}

	notify_contexts_mail_transaction_commit(t, changes_r);
	return 0;
}

static void
notify_transaction_rollback(struct mailbox_transaction_context *t)
{
	struct notify_transaction_context *lt = NOTIFY_CONTEXT(t);
	union mailbox_module_context *lbox = NOTIFY_CONTEXT(t->box);

	if (lt->tmp_mail != NULL)
		mail_free(&lt->tmp_mail);
	i_free(lt);
	
	notify_contexts_mail_transaction_rollback(t);
	lbox->super.transaction_rollback(t);
}

static struct mailbox *
notify_mailbox_alloc(struct mail_storage *storage, struct mailbox_list *list,
		     const char *name, struct istream *input,
		     enum mailbox_flags flags)
{
	union mail_storage_module_context *lstorage = NOTIFY_CONTEXT(storage);
	struct mailbox *box;
	union mailbox_module_context *lbox;

	box = lstorage->super.mailbox_alloc(storage, list, name, input, flags);

	lbox = p_new(box->pool, union mailbox_module_context, 1);
	lbox->super = box->v;

	box->v.mail_alloc = notify_mail_alloc;
	box->v.copy = notify_copy;
	box->v.save_begin = notify_save_begin;
	box->v.save_finish = notify_save_finish;
	box->v.transaction_begin = notify_transaction_begin;
	box->v.transaction_commit = notify_transaction_commit;
	box->v.transaction_rollback = notify_transaction_rollback;
	MODULE_CONTEXT_SET_SELF(box, notify_storage_module, lbox);
	return box;
}

static int
notify_mailbox_list_delete(struct mailbox_list *list, const char *name)
{
	union mailbox_list_module_context *llist = NOTIFY_LIST_CONTEXT(list);

	notify_contexts_mailbox_delete_begin(list, name);
	if (llist->super.delete_mailbox(list, name) < 0) {
		notify_contexts_mailbox_delete_rollback();
		return -1;
	}
	notify_contexts_mailbox_delete_commit(list, name);
	return 0;
}

static int
notify_mailbox_list_rename(struct mailbox_list *oldlist, const char *oldname,
			   struct mailbox_list *newlist, const char *newname,
			   bool rename_children)
{
	union mailbox_list_module_context *oldllist =
		NOTIFY_LIST_CONTEXT(oldlist);

	if (oldllist->super.rename_mailbox(oldlist, oldname, newlist, newname,
					   rename_children) < 0)
		return -1;

	notify_contexts_mailbox_rename(oldlist, oldname, newlist, newname,
				       rename_children);
	return 0;
}

static void notify_mail_storage_created(struct mail_storage *storage)
{
	union mail_storage_module_context *lstorage;

	lstorage = p_new(storage->pool, union mail_storage_module_context, 1);
	lstorage->super = storage->v;
	storage->v.mailbox_alloc = notify_mailbox_alloc;

	MODULE_CONTEXT_SET_SELF(storage, notify_storage_module, lstorage);

	if (notify_next_hook_mail_storage_created != NULL)
		notify_next_hook_mail_storage_created(storage);
}

static void notify_mailbox_list_created(struct mailbox_list *list)
{
	union mailbox_list_module_context *llist;

	llist = p_new(list->pool, union mailbox_list_module_context, 1);
	llist->super = list->v;
	list->v.delete_mailbox = notify_mailbox_list_delete;
	list->v.rename_mailbox = notify_mailbox_list_rename;

	MODULE_CONTEXT_SET_SELF(list, notify_mailbox_list_module, llist);

	if (notify_next_hook_mailbox_list_created != NULL)
		notify_next_hook_mailbox_list_created(list);
}

void notify_plugin_init_storage(void)
{
	notify_next_hook_mail_storage_created = hook_mail_storage_created;
	hook_mail_storage_created = notify_mail_storage_created;

	notify_next_hook_mailbox_list_created = hook_mailbox_list_created;
	hook_mailbox_list_created = notify_mailbox_list_created;
}

void notify_plugin_deinit_storage(void)
{
	hook_mail_storage_created = notify_next_hook_mail_storage_created;
	hook_mailbox_list_created = notify_next_hook_mailbox_list_created;
}
