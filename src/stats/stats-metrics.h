#ifndef STATS_METRICS_H
#define STATS_METRICS_H

struct stats_settings;

struct metric_field {
	const char *field_key;
	struct timing *timing;
};

struct metric {
	const char *name;

	/* Timing for how long the event existed */
	struct timing *duration_timing;

	unsigned int fields_count;
	struct metric_field *fields;
};

struct stats_metrics *stats_metrics_init(const struct stats_settings *set);
void stats_metrics_deinit(struct stats_metrics **metrics);

/* Returns event filter created from the stats_settings. */
struct event_filter *
stats_metrics_get_event_filter(struct stats_metrics *metrics);

/* Update metrics with given event. */
void stats_metrics_event(struct stats_metrics *metrics, struct event *event);

/* Iterate through all the tracked metrics. */
struct stats_metrics_iter *
stats_metrics_iterate_init(struct stats_metrics *metrics);
const struct metric *stats_metrics_iterate(struct stats_metrics_iter *iter);
void stats_metrics_iterate_deinit(struct stats_metrics_iter **iter);

#endif
