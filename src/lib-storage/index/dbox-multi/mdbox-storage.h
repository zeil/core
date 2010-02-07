#ifndef MDBOX_STORAGE_H
#define MDBOX_STORAGE_H

#include "index-storage.h"
#include "mailbox-list-private.h"
#include "dbox-storage.h"
#include "mdbox-settings.h"

#define MDBOX_STORAGE_NAME "mdbox"
#define MDBOX_GLOBAL_INDEX_PREFIX "dovecot.map.index"
#define MDBOX_GLOBAL_DIR_NAME "storage"
#define MDBOX_MAIL_FILE_PREFIX "m."
#define MDBOX_MAIL_FILE_FORMAT MDBOX_MAIL_FILE_PREFIX"%u"

#define MDBOX_INDEX_HEADER_MIN_SIZE (sizeof(uint32_t))
struct mdbox_index_header {
	uint32_t map_uid_validity;
	uint8_t mailbox_guid[MAIL_GUID_128_SIZE];
};

struct mdbox_storage {
	struct dbox_storage storage;
	union mailbox_list_module_context list_module_ctx;
	const struct mdbox_settings *set;

	/* paths for storage directories */
	const char *storage_dir, *alt_storage_dir;
	struct dbox_map *map;

	ARRAY_DEFINE(open_files, struct mdbox_file *);
};

struct mdbox_mail_index_record {
	uint32_t map_uid;
	/* UNIX timestamp of when the message was saved/copied to this
	   mailbox */
	uint32_t save_date;
};

struct mdbox_mailbox {
	struct mailbox box;
	struct mdbox_storage *storage;

	uint32_t map_uid_validity;
	uint32_t ext_id, hdr_ext_id, guid_ext_id;

	unsigned int creating:1;
};

extern struct mail_vfuncs mdbox_mail_vfuncs;

struct mailbox *
mdbox_mailbox_alloc(struct mail_storage *storage, struct mailbox_list *list,
		    const char *name, struct istream *input,
		    enum mailbox_flags flags);

int mdbox_mail_open(struct dbox_mail *mail, uoff_t *offset_r,
		    struct dbox_file **file_r);

/* Get map_uid for wanted message. */
int mdbox_mail_lookup(struct mdbox_mailbox *mbox, struct mail_index_view *view,
		      uint32_t seq, uint32_t *map_uid_r);
uint32_t dbox_get_uidvalidity_next(struct mailbox_list *list);
int mdbox_read_header(struct mdbox_mailbox *mbox,
		      struct mdbox_index_header *hdr);
void mdbox_update_header(struct mdbox_mailbox *mbox,
			 struct mail_index_transaction *trans,
			 const struct mailbox_update *update);

struct mail_save_context *
mdbox_save_alloc(struct mailbox_transaction_context *_t);
int mdbox_save_begin(struct mail_save_context *ctx, struct istream *input);
int mdbox_save_finish(struct mail_save_context *ctx);
void mdbox_save_cancel(struct mail_save_context *ctx);

struct dbox_file *
mdbox_save_file_get_file(struct mailbox_transaction_context *t,
			 uint32_t seq, uoff_t *offset_r);

int mdbox_transaction_save_commit_pre(struct mail_save_context *ctx);
void mdbox_transaction_save_commit_post(struct mail_save_context *ctx,
					struct mail_index_transaction_commit_result *result);
void mdbox_transaction_save_rollback(struct mail_save_context *ctx);

int mdbox_copy(struct mail_save_context *ctx, struct mail *mail);

#endif
