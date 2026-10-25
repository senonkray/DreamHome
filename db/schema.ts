import {sqliteTable,text,integer} from 'drizzle-orm/sqlite-core';
export const device=sqliteTable('luma_device',{
 id:text('id').primaryKey(), desired:text('desired').notNull().default('{}'),
 revision:integer('revision').notNull().default(0), applied:integer('applied').notNull().default(0),
 issued:integer('issued').notNull().default(0), seen:integer('seen').notNull().default(0),
 boot:text('boot').notNull().default(''), reported:text('reported').notNull().default('{}')
});
