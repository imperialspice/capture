
#include "main.hpp"

#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <ftxui/component/component.hpp>
#include <ftxui/component/component_base.hpp>
#include <ftxui/component/component_options.hpp>
#include <ftxui/component/event.hpp>
#include <ftxui/component/loop.hpp>
#include <ftxui/component/screen_interactive.hpp>
#include <ftxui/dom/direction.hpp>
#include <ftxui/dom/elements.hpp>
#include <ftxui/dom/node.hpp>
#include <ftxui/screen/screen.hpp>
#include <ftxui/dom/flexbox_config.hpp>
#include <functional>
#include <iostream>
#include <iterator>
#include <memory>
#include <sstream>
#include <string>
#include <string_view>
#include <unistd.h>
#include <vector>
#include <pwd.h>




std::string_view get_user_config_path(){
    const char* homedir;
    if((homedir = getenv("HOME")) == nullptr){
        homedir = getpwuid(getuid())->pw_dir;
    }

    return std::string_view(homedir);

}

void save_structure(struct_user_data &user_data, ftxui::ScreenInteractive& screen, bool closeOnSave){
    std::filesystem::path config_file_path(get_user_config_path());
    config_file_path.append("capture_config.json");
    const std::string output = rfl::json::write(user_data);

    std::ofstream config_file(config_file_path);
    if(config_file){
        config_file << output;
        config_file.close();
        if(closeOnSave){
            screen.PostEvent(ftxui::Event::CtrlC);
        }
    }
    else{
        screen.PostEvent(ftxui::Event::Special("custom_error_save"));
    }

}


struct_user_data load_structure(){
    std::filesystem::path config_file_path(get_user_config_path());
    config_file_path.append("capture_config.json");
    std::ifstream config_file(config_file_path);
    

    if(config_file){
        std::stringstream buffer;
        buffer << config_file.rdbuf();
        config_file.close();
        auto data = rfl::json::read<struct_user_data>(buffer.str());
        struct_user_data user_data = std::move(data.value());
        return user_data;
    }
    else{
        std::cerr << "Missing config file... Creating" << std::flush;
        struct_user_data user_data{};

        char tmp[100];
        getlogin_r(tmp, 100);
        user_data.username = std::string(tmp);
        user_data.name = std::string(tmp);
        return user_data;
    }


}




/*
    Main menu section
*/
std::vector<std::string> main_menu_entries = {
    "Edit Personal Information",
    "Manage SSH Keys",
    "Manage Virtual Machines"
};

/*
    Loop Info
*/



int main_menu_selection = 0;
ftxui::Component main_menu_menu;
ftxui::Element main_menu_document;
ftxui::Component main_menu_components;

ftxui::Component test_button;
ftxui::Component exit_button;
ftxui::Component back_button;
ftxui::Component save_button;
std::shared_ptr<ftxui::ComponentBase> main_menu_base;

/*
    User Edit Selection
*/
std::shared_ptr<ftxui::ComponentBase> user_edit_base;

/*
    SSH edit selection
*/
std::shared_ptr<ftxui::ComponentBase> ssh_key_edit_base;

/*
    SSH key add modal
*/

std::shared_ptr<ftxui::ComponentBase> ssh_key_dynamic_list(ftxui::Container::Vertical({}));


std::shared_ptr<ftxui::ComponentBase> ssh_add_key_modal_base;

/*
    File specific handlers
*/
std::shared_ptr<ftxui::ComponentBase> current_component;

/*
    Menu Choices
*/
int current_selection = -1;

void selector_function(){
    current_selection = main_menu_selection;
};

/*
    Show Modal
*/

bool bool_show_events_modal = true;

ftxui::Component events_modal(std::string &text_to_display, std::function<void()> exit_modal){
    // Components Layout
    ftxui::Component modal_container = ftxui::Container::Horizontal({
        ftxui::Button("Exit", exit_modal, ftxui::ButtonOption::Ascii())
    });

    // Element Layout
    auto modal_element_generator = [=](){
        return ftxui::flexbox({
            ftxui::text("An error occured"),
            ftxui::text(text_to_display),
            modal_container->Render()
        });
    };

    return ftxui::Renderer(modal_container, modal_element_generator);
}

/*
    SSH Key Modal Strings
*/

std::string modal_ssh_key_add_name("");
std::string modal_ssh_key_add_key("");
bool bool_show_add_ssh_key_modal = false;

/*
    General Update
*/

bool bool_update_components = false;



int main() {


    struct_user_data user_data = load_structure();

    // manage the screen object
    ftxui::ScreenInteractive screen = ftxui::ScreenInteractive::TerminalOutput();

    /*
        Navigation Buttons
    */


    /*
        As functional generators -- Stupid way of programming
    */
    const auto get_exit_button = [&]{
        return ftxui::Button("Exit", screen.ExitLoopClosure(), ftxui::ButtonOption::Ascii());
    };

    const auto get_test_button = [&]{
        return ftxui::Button("Test", [&]{bool_show_events_modal = true;}, ftxui::ButtonOption::Ascii());
    };

    const auto get_back_button = [&]{
        return ftxui::Button("Back", [&]{current_selection = -1;}, ftxui::ButtonOption::Ascii());
    };

    test_button = get_test_button();
    exit_button = get_exit_button();
    back_button = get_back_button();
    save_button = ftxui::Button("Save & Exit", [&]{
        save_structure(user_data, screen, true); 
       
    }, ftxui::ButtonOption::Ascii());

    const auto get_button_add_ssh_key = [&]{
            return ftxui::Button("Add Key", [&]{
            bool_show_add_ssh_key_modal = true;
            current_selection = -2;
        }, ftxui::ButtonOption::Ascii());
    };



    /*
        Main menu components
    */



    main_menu_menu = ftxui::Menu({
        .entries = &main_menu_entries,
        .selected = &main_menu_selection,
        .on_enter = selector_function
    });


    /*
        Full Document Layout
    */

    // Element
    auto main_menu_elements = [&]{ return ftxui::flexbox(
        {
                ftxui::flexbox({ 
                    ftxui::text("Welcome, " + user_data.name) | ftxui::underlined,
                    ftxui::filler(),
                    test_button->Render()
                },{
                    .direction = ftxui::FlexboxConfig::Direction::Row,
                    .align_items = ftxui::FlexboxConfig::AlignItems::Center,
                    .align_content = ftxui::FlexboxConfig::AlignContent::SpaceBetween,
                }) | ftxui::border | ftxui::size(ftxui::WIDTH, ftxui::GREATER_THAN, 90),
                ftxui::separatorEmpty(),
                ftxui::vflow({
                    ftxui::text("The current active virtual machine is accessable at:"),
                    ftxui::text(user_data.vm_ssh_address) | ftxui::bold | ftxui::underlined
                }),
                ftxui::separatorEmpty(),
                main_menu_menu->Render(),
                ftxui::filler(),
                ftxui::flexbox({ 
                    save_button->Render(),
                    ftxui::filler(),
                    exit_button->Render(),
                },{
                    .direction = ftxui::FlexboxConfig::Direction::Row,
                    .align_items = ftxui::FlexboxConfig::AlignItems::Center,
                    .align_content = ftxui::FlexboxConfig::AlignContent::SpaceBetween,
                }) | ftxui::border | ftxui::size(ftxui::WIDTH, ftxui::GREATER_THAN, 90) ,

        },
        {
            .direction = ftxui::FlexboxConfig::Direction::Column,
            .align_items = ftxui::FlexboxConfig::AlignItems::FlexStart,
            .align_content = ftxui::FlexboxConfig::AlignContent::FlexStart,

        });
    };
 
    // Component

    main_menu_components = ftxui::Container::Vertical({ 
        test_button,
        main_menu_menu,
        ftxui::Container::Horizontal({
            save_button,
            exit_button,
        })
        
    });



    /*
        User Edit Document Layer
    */

    auto user_edit_back_button = get_back_button();

    // declare inputs
    ftxui::Component name_field = ftxui::Input(&user_data.name, "John Smith", {
        .multiline = false,
    });
    ftxui::Component username_field = ftxui::Input(&user_data.username, "jsmith", {
        .multiline = false,
    });


    const auto get_user_edit = [&]{
        return ftxui::flexbox({
             ftxui::flexbox({ 
                    ftxui::text("Welcome, " + user_data.name) | ftxui::underlined,
                    ftxui::filler(),
                    user_edit_back_button->Render(),
                },{
                    .direction = ftxui::FlexboxConfig::Direction::Row,
                    .align_items = ftxui::FlexboxConfig::AlignItems::Center,
                    .align_content = ftxui::FlexboxConfig::AlignContent::SpaceBetween,
                }) | ftxui::border | ftxui::size(ftxui::WIDTH, ftxui::GREATER_THAN, 90),
                 ftxui::separatorEmpty(),
                 ftxui::paragraph("Please change your details below:\nThey will update automatically when you leave the field."),
                 ftxui::separatorEmpty(),
                 ftxui::vbox({
                    ftxui::hbox(ftxui::text("Name      : "), name_field->Render()),
                    ftxui::hbox(ftxui::text("Username  : "), username_field->Render())
                })
        }, {
            .direction = ftxui::FlexboxConfig::Direction::Column,
            .align_items = ftxui::FlexboxConfig::AlignItems::FlexStart,
            .align_content = ftxui::FlexboxConfig::AlignContent::FlexStart
        });
    };


    auto user_edit_components = ftxui::Container::Vertical({
        user_edit_back_button,
        name_field, 
        username_field
    });

    /*
        Manage SSH Keys
    */


 
    const auto get_ssh_edit_components = [&](ftxui::Component ssh_edit_components){
        ssh_edit_components->DetachAllChildren();
        
        log_vector.emplace_back("called get_ssh_edit_components");
        // auto ssh_edit_components = ftxui::Container::Vertical({});
        std::vector<ftxui::Element> ssh_edit_elements;
        

        // Itterate over all of the ssh keys in the struct and add it to the list.

        for(auto it = user_data.public_keys.begin(); it != user_data.public_keys.end(); ++it){
            int frame_position = std::distance(user_data.public_keys.begin(), it);

            auto get_removal_button =  [&]{
                return ftxui::Button("Remove", [&, frame_position, it]{
    
                    // user_data.public_keys.erase(std::next(user_data.public_keys.begin(), frame_position));
                    user_data.public_keys.erase(it);
                    bool_update_components = true;

                }, ftxui::ButtonOption::Ascii());
            };
            
            
            auto button = get_removal_button();
            
            std::string local_name = it->name;
            std::string local_key = it->key;
         
            
         
            auto modal_text = [local_name, local_key, frame_position, button](){    
                return (ftxui::flexbox({
                    ftxui::text(local_name),
                    ftxui::filler(),
                    ftxui::text(local_key),
                    ftxui::filler(),
                    button->Render()
                },{
                    .direction = ftxui::FlexboxConfig::Direction::Row,
                    .align_content = ftxui::FlexboxConfig::AlignContent::SpaceBetween
                })| ftxui::size(ftxui::WIDTH, ftxui::GREATER_THAN, 90));
            };

            // ssh_edit_elements.push_back(modal_text());
            ssh_edit_components->Add(ftxui::Renderer(button, [=]{
                return modal_text();
            }));
        }
        
        // auto modal_as_component = ftxui::Renderer(
        //         ssh_edit_components, [ssh_edit_elements]{
        //             return ftxui::vbox(ssh_edit_elements);
        // });

        // ssh_edit_components->Add(ftxui::Container::Vertical({
        //         modal_as_component
        // }));
    };

    
    const auto get_ssh_edit_components_all = [](ftxui::Component local_back_button,
        ftxui::Component local_button_add_ssh_key
        // ftxui::Component dynamic_list
        ){
        // IMPORTANT TO populate the dynamic list caller before this gets called.
  
        log_vector.emplace_back(std::format("list is of size : {}", ssh_key_dynamic_list->ChildCount()));

        auto rtn = ftxui::Container::Vertical({
            local_back_button,
            local_button_add_ssh_key,
        });
        rtn->Add(ssh_key_dynamic_list);

        return rtn;
    };

    // Show the addition modal when clicked to add a key
    const auto update_ssh_key_dynamic = [&](){
        // ssh_key_dynamic_list = get_ssh_edit_components();
        
        
        get_ssh_edit_components(ssh_key_dynamic_list);

        auto local_back_button = get_back_button();
        auto local_add_ssh_key = get_button_add_ssh_key();


    
        ssh_key_edit_base = ftxui::Renderer(get_ssh_edit_components_all(local_back_button, local_add_ssh_key), [=]{
        return ftxui::flexbox({
             ftxui::flexbox({ 
                    ftxui::text("Welcome, " + user_data.name) | ftxui::underlined,
                    ftxui::filler(),
                    local_back_button->Render(),
                },{
                    .direction = ftxui::FlexboxConfig::Direction::Row,
                    .align_items = ftxui::FlexboxConfig::AlignItems::Center,
                    .align_content = ftxui::FlexboxConfig::AlignContent::SpaceBetween,
                }) | ftxui::border | ftxui::size(ftxui::WIDTH, ftxui::GREATER_THAN, 90),
                 local_add_ssh_key->Render(),
                 ftxui::separatorEmpty(),
                ssh_key_dynamic_list->Render(),
                 ftxui::separatorEmpty()
        }, {
            .direction = ftxui::FlexboxConfig::Direction::Column,
            .align_items = ftxui::FlexboxConfig::AlignItems::FlexStart,
            .align_content = ftxui::FlexboxConfig::AlignContent::FlexStart
        });
        });
    };



    /*
        SSH KEY ADD MODAL 
    */

    ftxui::Component name_input = ftxui::Input(&modal_ssh_key_add_name, "Key Name", {
            .multiline = false,
    });
    ftxui::Component key_input = ftxui::Input(&modal_ssh_key_add_key, "Public Key", {
            .multiline = true,
    });
    
    ftxui::Component modal_save_and_exit_button = ftxui::Button("Save", [&]{
        current_selection = 1;
        bool_show_add_ssh_key_modal = false;
    
        user_data.public_keys.emplace_back(std::move(ssh_key{
            modal_ssh_key_add_name,
            modal_ssh_key_add_key
        }));

        get_ssh_edit_components(ssh_key_dynamic_list);

    }, ftxui::ButtonOption::Ascii());
    ftxui::Component modal_exit_button = ftxui::Button("Back", [&]{
        current_selection = 1;
        bool_show_add_ssh_key_modal = false;

    }, ftxui::ButtonOption::Ascii());

    ftxui::Component ssh_key_container = ftxui::Container::Vertical({
        name_input,
        key_input,
        ftxui::Container::Horizontal({
            modal_save_and_exit_button,
            modal_exit_button
        })
    });

    auto ssh_key_add_elements = [&] {
        return (ftxui::flexbox({
            ftxui::text("Add a Public Key"),
            name_input->Render(),
            key_input->Render(),
            ftxui::hbox({
                modal_save_and_exit_button->Render(),
                modal_exit_button->Render()
            })
        }, {
            .direction = ftxui::FlexboxConfig::Direction::Column,
            .align_items = ftxui::FlexboxConfig::AlignItems::Center,
            .align_content = ftxui::FlexboxConfig::AlignContent::SpaceBetween,
        }) | ftxui::border | ftxui::size(ftxui::WIDTH, ftxui::GREATER_THAN, 60));
    };

    // ssh key edit modal
   ssh_add_key_modal_base = ftxui::Renderer(ssh_key_container, ssh_key_add_elements);


    // ftxui::Render(screen, document);
    
    main_menu_base = ftxui::Renderer(main_menu_components, main_menu_elements);

    // user edit component

    user_edit_base = ftxui::Renderer(user_edit_components, get_user_edit);

    // ssh key edit component

    update_ssh_key_dynamic();

    // events modal component
    std::string current_error_message;
 

    auto show_events_modal = [&]{bool_show_events_modal = true; };
    auto exit_events_modal = [&]{bool_show_events_modal = false; };

    ftxui::Component events_modal_base = events_modal(current_error_message, exit_events_modal);

    const auto generate_primary_component = [&]{

        auto test = ftxui::Container::Vertical({
                ftxui::Maybe(main_menu_base, [&]{ return current_selection == -1;}),
                ftxui::Maybe(user_edit_base, [&]{ return current_selection == 0;}),
                ftxui::Maybe(ssh_key_edit_base, [&]{ return current_selection == 1;}),
                // ssh_key_add_modal([]{}, []{}),
            });

            /*
                Event Handler
            */

            ftxui::Component event_catcher = ftxui::CatchEvent(test, [&](ftxui::Event event){
                if(event == ftxui::Event::Special("custom_error_save")){
                    bool_show_events_modal = true;
                    current_error_message = "Error when attempting to save config file.";
                    return true;
                }
                return false;
            });

            /*
                Inject custom modals into the rendering pipeline, this
            */
            test->Add(
                (ftxui::Maybe(ssh_add_key_modal_base, &bool_show_add_ssh_key_modal) | ftxui::clear_under | ftxui::center)
            );

            
            test->Add(
                (ftxui::Maybe(events_modal_base, &bool_show_events_modal) | ftxui::clear_under | ftxui::center)
            );
            return test;
    };
    
    std::shared_ptr<ftxui::ComponentBase> test = generate_primary_component();

    ftxui::Loop loop = ftxui::Loop(&screen, test);

    while (!loop.HasQuitted()) {
        loop.RunOnceBlocking();
        if(bool_update_components){
            get_ssh_edit_components(ssh_key_dynamic_list);
            bool_update_components = false;
        }
        
        /*
            Call out of line updating functions?
        */

    }

    for(int i = 0; i < 100; i++){
        std::cout << "\n";
    }
    for(auto str : log_vector){
        std::cout << str << std::endl;
    }

    return 0;
}