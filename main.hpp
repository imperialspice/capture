#include <ftxui/dom/elements.hpp>
#include <ftxui/component/component.hpp>
#include <ftxui/component/screen_interactive.hpp>
#include "ftxui/component/captured_mouse.hpp"  // for ftxui
#include "ftxui/component/component.hpp"  // for Button, Horizontal, Renderer
#include "ftxui/component/component_base.hpp"      // for ComponentBase
#include "ftxui/component/component_options.hpp"   // for ButtonOption
#include "ftxui/component/screen_interactive.hpp"  // for ScreenInteractive
#include "ftxui/dom/elements.hpp"  // for gauge, separator, text, vbox, operator|, Element, border
#include "ftxui/screen/color.hpp" 
#include <iostream>
#include <memory>
#include <rfl.hpp>
#include <rfl/json.hpp>

/*
    Log capturing
*/
std::vector<std::string> log_vector;

struct ssh_key{
    std::string name;
    std::string key;
};

// read only struct, not write safe?
struct struct_user_data {
    std::string name;
    std::string vm_ssh_address;
    std::string username;
    std::string virtual_machine_id;
    std::vector<ssh_key> public_keys;
};


std::string_view get_user_config_path();
void save_structure(struct_user_data &user_data);
struct_user_data load_structure();