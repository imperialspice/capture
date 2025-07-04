#include "main.hpp"

#include "rfl.hpp"
#include "rfl/json.hpp"
#include <cstdio>
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
#include <ftxui/dom/selection.hpp>
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


/*
    Config variables
*/

std::string vm_create_template("virt-install \
    --name {}                           \
    --description {}                    \
    --ram={}                            \
    --vcpus={}                          \
    --os-type=Linux                     \
    --osinfo=linux2024                  \
    --disk path=size={},backing_store={},bus=virtio \
    --graphics none                     \
    --cloud-init user-data={}           \
    --noautoconsole"
);

std::string vm_cloud_config_template(R"(
#cloud-config
users:
  - name: {}
    sudo: ['ALL=(ALL) NOPASSWD:ALL']
    shell: '/bin/bash'
    ssh_authorized_keys:
      - {}

ssh_pwauth: false
chpasswd:
  expire: false
  users:
    - name: {}
      password: '1'
      type: text

allow_public_ssh_keys: true
disable_root: true)"
);

/*
    Add cloud image to config file.
*/

void add_cloud_image(struct_user_data &user_data, std::string_view cloud_img_name, std::string_view cloud_image_path){
    user_data.cloud_images.push_back(std::pair<std::string,std::string>(cloud_img_name, cloud_image_path));
}


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


struct_user_data load_structure(std::string_view directory_path, std::string_view file_name){
    std::filesystem::path config_file_path(directory_path);
    config_file_path.append(file_name);
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
        add_cloud_image(user_data, "ubuntu_debian", std::filesystem::path(directory_path).append("ubuntu_image.img").string());
        return user_data;
    }
}





/*
    Reference to vm cloud config selector
        - Requires restart to see new cloud config image
*/
int vm_cloud_selector = 0;
std::vector<std::string> vm_cloud_ref = {};
std::vector<std::string> vm_cloud_image_path = {};


void set_vm_cloud_ref_and_path(struct_user_data &user_data){
    vm_cloud_ref.clear();
    vm_cloud_image_path.clear();

    for(auto i : user_data.cloud_images){

        vm_cloud_ref.push_back(i.first);
        vm_cloud_image_path.push_back(i.second);

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
// events modal component
std::string current_error_message;

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

/*
    Open, Execute and Read from a command
*/

int current_command_result;
std::string current_command_output;

void open_execute_read(std::string command){
    std::array<char, 256> buffer;
    std::string result;

    FILE* pipe = popen(command.c_str(), "r");
    if(!pipe)
    {
        current_command_output = "";
        current_command_result = -1;
        return;
    }
    
    while (fgets(buffer.data(), 256, pipe) != NULL){
        result += buffer.data();
    }
    int return_code = pclose(pipe);

    current_command_output = result;
    current_command_result = return_code;
    return;
}


void create_vm_function(struct_user_data user_data, std::string create_vm_command, std::string tmp_cloud_config_text, std::string &current_error_message, std::string tmp_config_path){
        // run create vm, 
        // run virt-install and save results in config file...

       

        // tmp cloud config file.
        std::ofstream tmp_cloud_config(tmp_config_path);
        tmp_cloud_config << tmp_cloud_config_text;

        // temp config path injection

        open_execute_read(create_vm_command);
        // if command failed then put error up and log result?
        if(current_command_result != 0){
            bool_show_events_modal = true;
            current_error_message = "VM Creation failed : " + current_command_output;
        }
        // save current config into file.
        else{

        }
}

int main() {


    struct_user_data user_data = load_structure(get_user_config_path(), "capture_config.json");

    // manage the screen object
    ftxui::ScreenInteractive screen = ftxui::ScreenInteractive::TerminalOutput();

    /*
        Get component data from the config file.
    */

    set_vm_cloud_ref_and_path(user_data);

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
        Manage VM Page
    */

    const auto add_vm_button = ftxui::Button("Add VM", [&]{current_selection = 21;}, ftxui::ButtonOption::Ascii());
    
    
    const auto manage_vm_elements = [&]{
        return ftxui::flexbox({
            ftxui::flexbox({ 
                    add_vm_button->Render(),
                    ftxui::filler(),
                    back_button->Render()
                },{
                    .direction = ftxui::FlexboxConfig::Direction::Row,
                    .align_items = ftxui::FlexboxConfig::AlignItems::Center,
                    .align_content = ftxui::FlexboxConfig::AlignContent::SpaceBetween,
                }) | ftxui::border | ftxui::size(ftxui::WIDTH, ftxui::GREATER_THAN, 90),
        }) | ftxui::size(ftxui::WIDTH, ftxui::GREATER_THAN, 90);
    };

    const auto manage_vm_components = ftxui::Container::Vertical({
        ftxui::Container::Horizontal({
        add_vm_button,
        back_button
        })
    });
    /*
        Add new VM
    */


    /*
    Seperate Components
    */

    std::string vm_name;
    int vm_cpu = 0;
    int vm_memory = 0;
    std::string primary_disk_size = "";

    const auto vm_name_input = ftxui::Input(&vm_name, "VM Name", { .multiline = false, });
    const auto vm_set_cpu = ftxui::Slider("Number of CPUs : ", &vm_cpu, 0, 40, 1);
    const auto vm_set_memory = ftxui::Slider("Memory : ", &vm_memory, 1024, 80*1024, 1024);
    const auto vm_disk_size_input = ftxui::Input(&primary_disk_size, "100 GB", { .multiline = false, });

    const auto vm_cloud_config = ftxui::Dropdown(&vm_cloud_ref, &vm_cloud_selector);

    const auto create_vm = ftxui::Button("Create VM", [&]{
        
        
        std::string tmp_config_path("/tmp/cloud_config.tmp." + std::to_string(rand()));
        std::string vm_cloud_config = std::format(vm_cloud_config_template, user_data.username, user_data.public_keys.at(0).key, user_data.username);
        std::string vm_create_command_text = std::format(vm_create_template, vm_name, "Auto Created VM", vm_memory, vm_cpu, primary_disk_size, vm_cloud_image_path[vm_cloud_selector], tmp_config_path);

        create_vm_function(user_data, vm_create_command_text, vm_cloud_config, current_error_message, tmp_config_path);


    }, ftxui::ButtonOption::Ascii());

    const auto back_vm_page = ftxui::Button("Back", [&]{
        current_selection = 2;
    }, ftxui::ButtonOption::Ascii());

    const auto add_new_vm = [&]{
        return ftxui::flexbox({
            ftxui::flexbox({
                ftxui::text("Add New VM.")
            }),
            vm_name_input->Render() | ftxui::border,
            ftxui::flexbox({
                vm_set_cpu->Render() | ftxui::size(ftxui::WIDTH, ftxui::GREATER_THAN, 50),
                ftxui::filler(),
                ftxui::text(std::format("{} vCPUs ", vm_cpu)),
            },{
            }) | ftxui::size(ftxui::WIDTH, ftxui::GREATER_THAN, 70) | ftxui::border ,
            ftxui::flexbox({
                vm_set_memory->Render() | ftxui::size(ftxui::WIDTH, ftxui::GREATER_THAN, 50),
                ftxui::filler(),
                ftxui::text(std::format("Memory {} MB", vm_memory))
            }) | ftxui::size(ftxui::WIDTH, ftxui::GREATER_THAN, 70) | ftxui::border,
             ftxui::flexbox({
                vm_disk_size_input->Render() | ftxui::size(ftxui::WIDTH, ftxui::GREATER_THAN, 50),
                ftxui::text("GB"),
                ftxui::filler(),
            }) | ftxui::size(ftxui::WIDTH, ftxui::GREATER_THAN, 70) | ftxui::border,
             ftxui::flexbox({
                vm_cloud_config->Render() | ftxui::size(ftxui::WIDTH, ftxui::GREATER_THAN, 50),
                ftxui::filler(),
            }) | ftxui::size(ftxui::WIDTH, ftxui::GREATER_THAN, 70) | ftxui::border,
            ftxui::flexbox({
                create_vm->Render(),
                ftxui::filler(),
                back_vm_page->Render(),
            },{
                .align_content = ftxui::FlexboxConfig::AlignContent::SpaceBetween
            })

        },{
                    .direction = ftxui::FlexboxConfig::Direction::Column,
                    .align_items = ftxui::FlexboxConfig::AlignItems::Center,
                    .align_content = ftxui::FlexboxConfig::AlignContent::SpaceBetween,
        }) | ftxui::size(ftxui::WIDTH, ftxui::GREATER_THAN, 90) | ftxui::border;
    };

    const auto add_vm_components = ftxui::Container::Vertical({
        vm_name_input,
        vm_set_cpu,
        vm_set_memory,
        vm_disk_size_input,
        vm_cloud_config,
        ftxui::Container::Horizontal({
            create_vm,
            back_vm_page
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

    // vm management 

    const auto vm_management_base = ftxui::Renderer(manage_vm_components, manage_vm_elements);

    // vm add modal

    const auto vm_add_modal_base = ftxui::Renderer(add_vm_components, add_new_vm);

    // ssh key edit component

    update_ssh_key_dynamic();


 

    auto show_events_modal = [&]{bool_show_events_modal = true; };
    auto exit_events_modal = [&]{bool_show_events_modal = false; };

    ftxui::Component events_modal_base = events_modal(current_error_message, exit_events_modal);

    const auto generate_primary_component = [&]{

        auto test = ftxui::Container::Vertical({
                ftxui::Maybe(main_menu_base, [&]{ return current_selection == -1;}),
                ftxui::Maybe(user_edit_base, [&]{ return current_selection == 0;}),
                ftxui::Maybe(ssh_key_edit_base, [&]{ return current_selection == 1;}),
                ftxui::Maybe(vm_management_base, [&]{return current_selection == 2;}),
                ftxui::Maybe(vm_add_modal_base, [&]{return current_selection == 21;})
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