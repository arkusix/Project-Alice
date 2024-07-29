#include "serialization.hpp"
#include "system_state.hpp"

static sys::state game_state; // too big for the stack

// Beginning of code block from src/launcher/launcher_main.cpp
// All credit goes to those who contributed to the whole code and underlying logic
// To build the scenario file with mods, simply use "-build" parameters
// There should be a more elegant way to do this, despite the crudeness
// of this, this method produces the scenario file needed
// WARNING: all mods in mod folder will be processed, please keep
// only the mods you wish to play with inside the mod folder
struct scenario_file {
	native_string file_name;
	sys::mod_identifier ident;
};
// Utilizing the already established logic, perhaps, by iterating
// through a series of mod path to .mod files added into vector
// by parsing args params, e.g. -mod A.mod -mod A-Sub.mod -mod B.mod
static std::vector<parsers::mod_file> mod_list;
static native_string selected_scenario_file;
static uint32_t max_scenario_count = 0;
static std::vector<scenario_file> scenario_files;

// Keep mod_list empty for vanilla
native_string produce_mod_path() {
	simple_fs::file_system(dummy);
	simple_fs::add_root(dummy, NATIVE("."));
	for(int32_t i = 0; i < int32_t(mod_list.size()); ++i) {
		mod_list[i].add_to_file_system(dummy);
	}
	return simple_fs::extract_state(dummy);
}

// This function is used to parse all mod files into the mod_list vector
void check_mods_folder() {
	simple_fs::file_system fs_root;
	simple_fs::add_root(fs_root, ".");
	auto root = get_root(fs_root);
	auto mod_dir = simple_fs::open_directory(root, NATIVE("mod"));
	// TODO: Should implement selective mod_files
	auto mod_files = simple_fs::list_files(mod_dir, NATIVE(".mod"));
	parsers::error_handler err("");
	for (auto& f : mod_files) {
		auto of = simple_fs::open_file(f);
		if(of) {
			auto content = view_contents(*of);
			parsers::token_generator gen(content.data, content.data + content.file_size );
			mod_list.push_back(parsers::parse_mod_file(gen, err, parsers::mod_file_context{}));
		}
	}
}

native_string to_hex(uint64_t v) {
		native_string ret;
		constexpr native_char digits[] = NATIVE("0123456789ABCDEF");
		do {
		 ret += digits[v & 0x0F];
		 v = v >> 4;
		} while(v != 0);
		 	return ret;
	};

// Original is void make_mod_file(). This function can also be used to create
// vanilla scenario, though when there are two bookmarks dates, it will also create
// additional scenario file alongside with the save file for the bookmark.
// Unsure how it works in windows build.
void build_scenario_file() {
	auto path = produce_mod_path();
	simple_fs::file_system fs_root;
	simple_fs::restore_state(fs_root, path);
	parsers::error_handler err("");
	auto root = get_root(fs_root);
	auto common = open_directory(root, NATIVE("common"));
	parsers::bookmark_context bookmark_context;
	if(auto f = open_file(common, NATIVE("bookmarks.txt")); f) {
		auto bookmark_content = simple_fs::view_contents(*f);
		err.file_name = "bookmarks.txt";
		parsers::token_generator gen(bookmark_content.data, bookmark_content.data + bookmark_content.file_size);
		parsers::parse_bookmark_file(gen, err, bookmark_context);
		assert(!bookmark_context.bookmark_dates.empty());
	} else {
		err.accumulated_errors += "File common/bookmarks.txt could not be opened\n";
	}

	sys::checksum_key scenario_key;

	for(uint32_t date_index = 0; date_index < uint32_t(bookmark_context.bookmark_dates.size()); date_index++) {
		err.accumulated_errors.clear();
		err.accumulated_warnings.clear();
		//
		auto game_state = std::make_unique<sys::state>();
		simple_fs::restore_state(game_state->common_fs, path);
		game_state->load_scenario_data(err, bookmark_context.bookmark_dates[date_index].date_);
		if(err.fatal)
			break;
		if(date_index == 0) {
			auto sdir = simple_fs::get_or_create_scenario_directory();
			int32_t append = 0;
			auto time_stamp = uint64_t(std::time(0));
			auto base_name = to_hex(time_stamp);
			while(simple_fs::peek_file(sdir, base_name + NATIVE("-") + std::to_string(append) + NATIVE(".bin"))) {
				++append;
			}
			++max_scenario_count;
			selected_scenario_file = base_name + NATIVE("-") + std::to_string(append) + NATIVE(".bin");
			sys::write_scenario_file(*game_state, selected_scenario_file, max_scenario_count);
			if(auto of = simple_fs::open_file(sdir, selected_scenario_file); of) {
				auto content = view_contents(*of);
				auto desc = sys::extract_mod_information(reinterpret_cast<uint8_t const*>(content.data), content.file_size);
				if(desc.count != 0) {
					scenario_files.push_back(scenario_file{ selected_scenario_file , desc });
				}
			}
			std::sort(scenario_files.begin(), scenario_files.end(), [](scenario_file const& a, scenario_file const& b) {
				return a.ident.count > b.ident.count;
			});
			scenario_key = game_state->scenario_checksum;
		} else {
#ifndef NDEBUG
			sys::write_scenario_file(*game_state, std::to_string(date_index) + NATIVE(".bin"), 0);
#endif
			game_state->scenario_checksum = scenario_key;
			sys::write_save_file(*game_state, sys::save_type::bookmark, bookmark_context.bookmark_dates[date_index].name_);
		}
	}

	if(!err.accumulated_errors.empty() || !err.accumulated_warnings.empty()) {
		auto assembled_msg = std::string("You can still play the mod, but it might be unstable\r\nThe following problems were encountered while creating the scenario:\r\n\r\nErrors:\r\n") + err.accumulated_errors + "\r\n\r\nWarnings:\r\n" + err.accumulated_warnings;
		// Changed to output to stdout instead of making a file, this is temporary
		// Will change into making .log files stored at approriate directory
		window::emit_error_message(assembled_msg, false);
	}
}
//End block of code from src/launcher/launcher_main.cpp

int main(int argc, char **argv) {
	add_root(game_state.common_fs, NATIVE(".")); // will add the working directory as first root -- for the moment this lets us find the shader files

	if(argc >= 2) {
// The commented code below was probably used to quickly load certain scenarios to debug
// Here is the proposed method, not yet implemented properly, use -start path/to/scenario/files.bin
// Currently, the code looks for the scenario files inside ${HOME}/.local/share/Alice/scenarios/
// Thus the proper way of running it would be -start FNAME.bin, replace FNAME with your scenario filename

// #ifndef NDEBUG
//		{
//			auto msg = std::string("Loading scenario  ") + simple_fs::native_to_utf8(argv[1]) + "\n";
//			window::emit_error_message(msg, false);
//		}
//#endif
		for(int i = 1; i < argc; ++i) {
			if(native_string(argv[i]) == NATIVE("-host")) {
				game_state.network_mode = sys::network_mode_type::host;
			} else if(native_string(argv[i]) == NATIVE("-join")) {
				game_state.network_mode = sys::network_mode_type::client;
				game_state.network_state.ip_address = "127.0.0.1";
				if(i + 1 < argc) {
					game_state.network_state.ip_address = simple_fs::native_to_utf8(native_string(argv[i + 1]));
					i++;
				}
			} else if(native_string(argv[i]) == NATIVE("-name")) {
				if(i + 1 < argc) {
					std::string nickname = simple_fs::native_to_utf8(native_string(argv[i + 1]));
					memcpy(game_state.network_state.nickname.data, nickname.data(), std::min<size_t>(nickname.length(), 8));
					i++;
				}
			} else if(native_string(argv[i]) == NATIVE("-password")) {
				if(i + 1 < argc) {
					auto str = simple_fs::native_to_utf8(native_string(argv[i + 1]));
					std::memset(game_state.network_state.password, '\0', sizeof(game_state.network_state.password));
					std::memcpy(game_state.network_state.password, str.c_str(), std::min(sizeof(game_state.network_state.password), str.length()));
					i++;
				}
			} else if(native_string(argv[i]) == NATIVE("-v6")) {
				game_state.network_state.as_v6 = true;
			} else if(native_string(argv[i]) == NATIVE("-v4")) {
				game_state.network_state.as_v6 = false;
			} else if(native_string(argv[i]) == NATIVE("-build")) {
				// Populate mod_init vector
				check_mods_folder();
				// Self explanatory
				build_scenario_file();
				auto msg = ("Scenario files is saved to ${HOME}/.local/share/Alice/scenarios folder.\n");
				window::emit_error_message(msg, false);
				// Ensuring exit
				return 0;
			} else if(native_string(argv[i]) == NATIVE("-vanilla")) {
				build_scenario_file();
			} else if(native_string(argv[i]) == NATIVE("-start")) {
				if (i + 1 < argc) {
					// In the future path should point to the real absolute path of a scenario file
					auto path = simple_fs::native_to_utf8(argv[i + 1]);
					auto msg = "Loading scenario from: " + path + "\n";
					window::emit_error_message(msg, false);
					if (sys::try_read_scenario_and_save_file(game_state, path)) {
						game_state.fill_unsaved_data();
					} else {
						auto msg = std::string("Scenario file from: ") + path + " could not be read/found. \n";
						window::emit_error_message(msg, true);
						return 0;
					}
				} else {
					auto msg = std::string("Please provide a scenario file.\n");
					window::emit_error_message(msg, true);
					// Ensuring exit
					return 0;
				}

			}
		}

// Initially, this will create the development_test_file.bin and will load it into the game
//		if(sys::try_read_scenario_and_save_file(game_state, argv[1])) {
//			game_state.fill_unsaved_data();
//		} else {
//			auto msg = std::string("Scenario file ") + simple_fs::native_to_utf8(argv[1]) + " could not be read.\n";
//			window::emit_error_message(msg, true);
//			return 0;
//		}

		network::init(game_state);
	} else {
		// Falling back to vanilla, will create "development_test_file.bin" if ! exists
		if(!sys::try_read_scenario_and_save_file(game_state, NATIVE("development_test_file.bin"))) {
			// scenario making functions
			parsers::error_handler err{ "" };
			game_state.load_scenario_data(err, sys::year_month_day{ 1836, 1, 1 });
			if(!err.accumulated_errors.empty())
				window::emit_error_message(err.accumulated_errors, true);
			sys::write_scenario_file(game_state, NATIVE("development_test_file.bin"), 0);
			game_state.loaded_scenario_file = NATIVE("development_test_file.bin");
		} else {
			game_state.fill_unsaved_data();
		}
	}

	// scenario loading functions (would have to run these even when scenario is pre-built
	game_state.load_user_settings();
	ui::populate_definitions_map(game_state);

	std::thread update_thread([&]() { game_state.game_loop(); });
		
	// An idea, control window_state through another arg, e.g. -f/--fullscreen
	window::create_window(game_state, window::creation_parameters{1024, 780, window::window_state::maximized, game_state.user_settings.prefer_fullscreen});

	game_state.quit_signaled.store(true, std::memory_order_release);
	update_thread.join();

	network::finish(game_state, true);
	return EXIT_SUCCESS;
}
