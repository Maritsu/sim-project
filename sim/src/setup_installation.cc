#include <cppconn/prepared_statement.h>
#include <sim/db.h>
#include <simlib/debug.h>
#include <simlib/random.h>
#include <simlib/sha.h>
#include <simlib/string.h>

using std::string;
using std::unique_ptr;

static bool DROP_TABLES = false, ONLY_DROP_TABLES = false;

/**
 * @brief Displays help
 */
static void help(const char* program_name) {
	if (program_name == nullptr)
		program_name = "setup-installation";

	printf("Usage: %s [options] INSTALL_DIR", program_name);
	puts("Setup database after SIM installation");
	puts("");
	puts("Options:");
	puts("  --drop-tables          Drop database tables before recreating them");
	puts("  -h, --help             Display this information");
	puts("  --only-drop-tables     Drop database tables and exit");
}

static void parseOptions(int &argc, char **argv) {
	int new_argc = 1;
	for (int i = 1; i < argc; ++i) {

		if (argv[i][0] == '-') {
			// Drop tables
			if (0 == strcmp(argv[i], "--drop-tables"))
				DROP_TABLES = true;

			// Help
			else if (0 == strcmp(argv[i], "-h") ||
					0 == strcmp(argv[i], "--help")) {
				help(argv[0]); // argv[0] is valid (argc > 1)
				exit(0);
			}

			// Drop tables
			else if (0 == strcmp(argv[i], "--only-drop-tables")) {
				DROP_TABLES = true;
				ONLY_DROP_TABLES = true;
			}

			// Unknown
			else
				eprintf("Unknown option: '%s'\n", argv[i]);

		} else
			argv[new_argc++] = argv[i];
	}

	argc = new_argc;
}

int main(int argc, char **argv) {
	parseOptions(argc, argv);

	if(argc != 2) {
		help(argc > 0 ? argv[0] : nullptr);
		return 1;
	}

	DB::Connection conn;
	try {
		// Get connection
		conn = DB::createConnectionUsingPassFile(
			concat(argv[1], "/.db.config"));

	} catch (const std::exception& e) {
		eprintf("\033[31mFailed to connect to database\033[m - %s\n", e.what());
		return 4;
	}

	bool error = false;
	unique_ptr<sql::Statement> stmt(conn->createStatement());

	if (DROP_TABLES) {
		try {
			stmt->executeUpdate("DROP TABLE iF EXISTS users");
			stmt->executeUpdate("DROP TABLE iF EXISTS session");
			stmt->executeUpdate("DROP TABLE iF EXISTS problems");
			stmt->executeUpdate("DROP TABLE iF EXISTS rounds");
			stmt->executeUpdate("DROP TABLE iF EXISTS users_to_contests");
			stmt->executeUpdate("DROP TABLE iF EXISTS submissions");

			if (ONLY_DROP_TABLES)
				return 0;

		} catch (const std::exception& e) {
			eprintf("\033[31mFailed to drop tables\033[m - %s\n", e.what());
			return 5;
		}
	}

	// users
	try {
		stmt->executeUpdate("CREATE TABLE IF NOT EXISTS `users` (\n"
				"`id` int unsigned NOT NULL AUTO_INCREMENT,\n"
				"`username` varchar(30) COLLATE utf8_bin NOT NULL,\n"
				"`first_name` varchar(60) COLLATE utf8_bin NOT NULL,\n"
				"`last_name` varchar(60) COLLATE utf8_bin NOT NULL,\n"
				"`email` varchar(60) COLLATE utf8_bin NOT NULL,\n"
				"`salt` char(64) COLLATE utf8_bin NOT NULL,\n"
				"`password` char(128) COLLATE utf8_bin NOT NULL,\n"
				"`type` tinyint(1) unsigned NOT NULL DEFAULT 2,\n"
				"PRIMARY KEY (`id`),\n"
				"UNIQUE KEY `username` (`username`)\n"
			") ENGINE=MyISAM DEFAULT CHARSET=utf8 COLLATE=utf8_bin\n");

			// Add default user sim with password sim
			char salt_bin[32];
			fillRandomly(salt_bin, 32);
			string salt = toHex(salt_bin, 32);

			unique_ptr<sql::PreparedStatement> pstmt(conn->
				prepareStatement("INSERT IGNORE INTO users "
					"(username, salt, password, type) VALUES ('sim', ?, ?, 0)"));
			pstmt->setString(1, salt);
			pstmt->setString(2, sha3_512(salt + "sim"));
			pstmt->executeUpdate();

	} catch (const std::exception& e) {
		eprintf("\033[31mFailed to create table `users`\033[m - %s\n", e.what());
		error = true;
	}

	// session
	try {
		stmt->executeUpdate("CREATE TABLE IF NOT EXISTS `session` (\n"
				"`id` char(30) COLLATE utf8_bin NOT NULL,\n"
				"`user_id` int unsigned NOT NULL,\n"
				"`data` text COLLATE utf8_bin NOT NULL,\n"
				"`ip` char(15) COLLATE utf8_bin NOT NULL,\n"
				"`user_agent` text COLLATE utf8_bin NOT NULL,\n"
				"`time` datetime NOT NULL,\n"
				"PRIMARY KEY (`id`),\n"
				"KEY (`user_id`),\n"
				"KEY (`time`)\n"
			") ENGINE=MyISAM DEFAULT CHARSET=utf8 COLLATE=utf8_bin;\n");

	} catch (const std::exception& e) {
		eprintf("\033[31mFailed to create table `session`\033[m - %s\n", e.what());
		error = true;
	}

	// problems
	try {
		stmt->executeUpdate("CREATE TABLE IF NOT EXISTS `problems` (\n"
				"`id` int unsigned NOT NULL AUTO_INCREMENT,\n"
				"`access` enum('public', 'private') COLLATE utf8_bin NOT NULL DEFAULT 'private',\n"
				"`name` VARCHAR(128) NOT NULL,\n"
				"`tag` CHAR(4) NOT NULL,\n"
				"`owner` int unsigned NOT NULL,\n"
				"`added` datetime NOT NULL,\n"
				"PRIMARY KEY (`id`),\n"
				"KEY (`owner`)\n"
			") ENGINE=MyISAM DEFAULT CHARSET=utf8 COLLATE=utf8_bin");

	} catch (const std::exception& e) {
		eprintf("\033[31mFailed to create table `problems`\033[m - %s\n", e.what());
		error = true;
	}

	// rounds
	try {
		stmt->executeUpdate("CREATE TABLE IF NOT EXISTS `rounds` (\n"
				"`id` int unsigned NOT NULL AUTO_INCREMENT,\n"
				"`parent` int unsigned NULL DEFAULT NULL,\n"
				"`grandparent` int unsigned NULL DEFAULT NULL,\n"
				"`problem_id` int unsigned DEFAULT NULL,\n"
				"`access` enum('public', 'private') COLLATE utf8_bin NOT NULL DEFAULT 'private',\n"
				"`name` VARCHAR(128) NOT NULL,\n"
				"`owner` int unsigned NOT NULL,\n"
				"`item` int unsigned NOT NULL,\n"
				"`visible` BOOLEAN NOT NULL DEFAULT FALSE,\n"
				"`show_ranking` BOOLEAN NOT NULL DEFAULT FALSE,\n"
				"`begins` datetime NULL DEFAULT NULL,\n"
				"`full_results` datetime NULL DEFAULT NULL,\n"
				"`ends` datetime NULL DEFAULT NULL,\n"
				"PRIMARY KEY (`id`),\n"
				"KEY (`parent`, `visible`),\n"
				"KEY (`parent`, `begins`),\n"
				"KEY (`parent`, `access`),\n"
				"KEY (`grandparent`, `item`),\n"
				"KEY (`owner`)\n"
			") ENGINE=MyISAM DEFAULT CHARSET=utf8 COLLATE=utf8_bin");

	} catch (const std::exception& e) {
		eprintf("\033[31mFailed to create table `rounds`\033[m - %s\n", e.what());
		error = true;
	}

	// users_to_contests
	try {
		stmt->executeUpdate("CREATE TABLE IF NOT EXISTS `users_to_contests` (\n"
				"`user_id` int unsigned NOT NULL,\n"
				"`contest_id` int unsigned NOT NULL,\n"
				"PRIMARY KEY (`user_id`, `contest_id`),\n"
				"KEY (`contest_id`)\n"
			") ENGINE=MyISAM DEFAULT CHARSET=utf8 COLLATE=utf8_bin");

	} catch (const std::exception& e) {
		eprintf("\033[31mFailed to create table `users_to_rounds`\033[m - %s\n",
			e.what());
		error = true;
	}

	// submissions
	try {
		stmt->executeUpdate("CREATE TABLE IF NOT EXISTS `submissions` (\n"
				"`id` int unsigned NOT NULL AUTO_INCREMENT,\n"
				"`user_id` int unsigned NOT NULL,\n"
				"`problem_id` int unsigned NOT NULL,\n"
				"`round_id` int unsigned NOT NULL,\n"
				"`parent_round_id` int unsigned NOT NULL,\n"
				"`contest_round_id` int unsigned NOT NULL,\n"
				"`final` BOOLEAN NOT NULL DEFAULT FALSE,\n"
				"`submit_time` datetime NOT NULL,\n"
				"`status` enum('ok','error','c_error','judge_error','waiting') NULL DEFAULT NULL COLLATE utf8_bin,\n"
				"`score` int NULL DEFAULT NULL,\n"
				"`queued` datetime NOT NULL,\n"
				"`initial_report` text COLLATE utf8_bin NOT NULL,\n"
				"`final_report` text COLLATE utf8_bin NOT NULL,\n"
				"PRIMARY KEY (id),\n"
				// Judge server
				"KEY (status, queued),\n"
				// Update final, delete account
				"KEY (user_id, round_id, status, id),\n"
				// Contest::submissions() - view all
				"KEY (round_id, id),\n"
				"KEY (round_id, user_id, id),\n"
				"KEY (parent_round_id, id),\n"
				"KEY (parent_round_id, user_id, id),\n"
				"KEY (contest_round_id, id),\n"
				"KEY (contest_round_id, user_id, id),\n"
				// Contest::submissions() - view only finals
				"KEY (round_id, final, id),\n"
				"KEY (round_id, user_id, final, id),\n"
				"KEY (parent_round_id, final, id),\n"
				"KEY (parent_round_id, user_id, final, id),\n"
				"KEY (contest_round_id, final, id),\n"
				"KEY (contest_round_id, user_id, final, id)\n"
			") ENGINE=MyISAM DEFAULT CHARSET=utf8 COLLATE=utf8_bin");

	} catch (const std::exception& e) {
		eprintf("\033[31mFailed to create table `submissions`\033[m - %s\n",
			e.what());
		error = true;
	}

	if (error)
		return 6;

	return 0;
}
