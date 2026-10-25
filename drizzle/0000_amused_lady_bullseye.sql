CREATE TABLE `luma_device` (
	`id` text PRIMARY KEY NOT NULL,
	`desired` text DEFAULT '{}' NOT NULL,
	`revision` integer DEFAULT 0 NOT NULL,
	`applied` integer DEFAULT 0 NOT NULL,
	`issued` integer DEFAULT 0 NOT NULL,
	`seen` integer DEFAULT 0 NOT NULL,
	`boot` text DEFAULT '' NOT NULL,
	`reported` text DEFAULT '{}' NOT NULL
);
