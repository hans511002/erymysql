DROP FUNCTION IF EXISTS last_insert_grn_id;
DROP FUNCTION IF EXISTS mroonga_snippet;
DROP FUNCTION IF EXISTS mroonga_command;
DROP FUNCTION IF EXISTS mroonga_escape;

UNINSTALL PLUGIN Mroonga;

FLUSH TABLES;
