#include "main.hpp"

#include <sys/poll.h>
#include <sys/unistd.h>
#include <sys/fcntl.h>
#include <csignal>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <format>
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
#include <ftxui/screen/color.hpp>
#include <ftxui/screen/screen.hpp>
#include <ftxui/dom/flexbox_config.hpp>
#include <functional>
#include <iostream>
#include <iterator>
#include <memory>
#include <sstream>
#include <string>
#include <string_view>
#include <sys/select.h>
#include <unistd.h>
#include <vector>
#include <pwd.h>

// write log out and exit 
void flush_log(){
    std::filesystem::path logfile_file_path("/tmp");
    logfile_file_path.append(std::format("capture_logfile_{}.log", rand()));
    std::ofstream log_file(logfile_file_path);
    if(log_file){
        for(auto &log : log_vector){
            log_file << log << "\n";
        }
        log_file.close();
    }
}

void capture_flush_log(int signum){
    flush_log();
    exit(signum);
}

std::string vm_create_template = R"(virt-install \
    --name '{}' \
    --description '{}' \
    --ram={} \
    --vcpus={} \
    --osinfo=linux2024 \
    --disk size={},backing_store='{}',bus=virtio \
    --graphics none \
    --cloud-init user-data='{}' \
    --noautoconsole )";

const char * vm_cloud_config_template = R"(#cloud-config
users:
  - name: {}
    sudo: ['ALL=(ALL) NOPASSWD:ALL']
    shell: '/bin/bash'
    ssh_authorized_keys:
      - {}

ssh_pwauth: false
chpasswd:
  expire: true
  users:
    - {{name: {}, password: newpassword, type: text}}
allow_public_ssh_keys: true
disable_root: true)";

std::string vm_get_current_state = R"(virsh dominfo {} 2>/dev/null | grep State | awk '{{for (i = 2; i <= NF; i++) {{printf "%s ", $i}}; printf "\n"}}')";
std::string podman_add_remote = R"(podman system connection add [my-remote-machine] --identity [your private key] ssh://[username]@[server_ip]{})";


/*
    Save since last update
*/

bool has_been_saved_since_last_update = true;


/*
    Out of loop, system execution.
*/

bool bool_launch_system_execution = false;
std::string launch_system_execution_path = "";

/*
    Add cloud image to config file.
*/

void add_cloud_image(struct_user_data &user_data, std::string_view cloud_img_name, std::string_view cloud_image_path){
    has_been_saved_since_last_update = false;
    user_data.cloud_images.push_back(std::pair<std::string,std::string>(cloud_img_name, cloud_image_path));
}

std::string_view get_user_config_path(){
    const char* homedir;
    if((homedir = getenv("HOME")) == nullptr){
        homedir = getpwuid(getuid())->pw_dir;
    }

    return std::string_view(homedir);

}

void save_structure(struct_user_data &user_data){
    has_been_saved_since_last_update = true;
    std::filesystem::path config_file_path(get_user_config_path());
    config_file_path.append("capture_config.json");
    const std::string output = rfl::json::write(user_data);

    std::ofstream config_file(config_file_path);
    if(config_file){
        config_file << output;
        config_file.close();
    }
}

void save_structure(struct_user_data &user_data, ftxui::ScreenInteractive& screen, bool closeOnSave){
    has_been_saved_since_last_update = true;

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


struct_general_config load_general_config(std::string_view directory_path, std::string_view file_name){
    
    // try and open general config in the same directory and then check if in installed path
    std::filesystem::path local_config_file_path("./"); local_config_file_path.append(file_name);
    std::filesystem::path config_file_path(directory_path); config_file_path.append(file_name);
    std::ifstream config_file;

    if(std::filesystem::exists(local_config_file_path)){
        config_file.open(local_config_file_path);    
    }
    else if(std::filesystem::exists(config_file_path)){
        config_file.open(config_file_path);
    }
      
    if(config_file){
        std::stringstream buffer;
        buffer << config_file.rdbuf();
        config_file.close();
        auto data = rfl::json::read<struct_general_config>(buffer.str());
        struct_general_config config = std::move(data.value());
        return config;
    }
    else{
        std::cerr << "Missing general config file... Creating" << std::flush;
        struct_general_config config{};

        config.version = 1;
        config.default_image_path = "/opt/disks";
        config.search_directory = true;
        return config;
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

/*
    SSH Key Selector
*/

std::vector<std::string> vm_ssh_key_names; // updated when creating vm_create view.
int vm_ssh_key_selector;

/*
    Collect info about the current cloud images and paths
*/
void set_vm_cloud_ref_and_path(struct_general_config &general_config){
    vm_cloud_ref.clear();
    vm_cloud_image_path.clear();
    if(!std::filesystem::exists(general_config.default_image_path)) return;
    for (const auto &disk : std::filesystem::directory_iterator(general_config.default_image_path)){
        if(disk.path().extension() == ".img" || disk.path().extension() == ".iso") {
            vm_cloud_ref.push_back(disk.path().stem());
            vm_cloud_image_path.push_back(disk.path());    
        }
    }
}

/*
    Main menu section
*/
std::vector<std::string> main_menu_entries = {
    "Edit Personal Information",
    "Manage SSH Keys",
    "Manage Virtual Machines",
    "Manage Containers"
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
    VM Mange Screen
*/


std::shared_ptr<ftxui::ComponentBase> current_vm_manage_base;

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

/*
    Error Component
*/

ftxui::Component events_modal_base;
bool bool_show_events_modal = false;
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

void update_error_modal(std::string text_to_display){
    
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
std::string current_command_err;

void open_execute_read(std::string command, bool capture_stderr = true){

    current_command_err = "";
    current_command_output = "";

    std::array<char, 256> buffer;
    std::string result;
    log_vector.push_back("command was : " + command);
    
    // capture std::err too.
    bool is_capturing_stderr = capture_stderr;
    int pfds[2];
    FILE * perr;

    if(pipe(pfds) < 0){ current_command_result = -1; return; }
    
    if(is_capturing_stderr){
        command = command + std::format(" 2>&{}", pfds[1]);
    }
    
    fcntl(pfds[0], F_SETFL, O_NONBLOCK);

    perr = fdopen(pfds[0], "r");
    
    FILE* pipe = popen(command.c_str(), "r");

    if(!pipe)
    {
        // if capturing set up 
        
        close(pfds[0]); close(pfds[1]);
    
        current_command_err = "Pipe open failure";
        current_command_output = "";
        current_command_result = -1;
        return;
    }
    



    struct timeval timeout{1, 0};
    struct pollfd fds[2];

    fds[0].fd = pfds[0];
    fds[0].events = POLLIN;

    fds[1].fd = fileno(pipe);
    fds[1].events = POLLIN;



    int captured_data = 0;
    while(captured_data < 50){

        if(poll(fds,2, 2000) == -1){
            perror("Select has crashed...");
            break;
        }

        if(fds[1].revents & POLLIN){
            int read_len = 0;
            while((read_len = read(fileno(pipe), buffer.data(), 255)) > 0){
                std::string tmp; 
                tmp.append(buffer.data(), read_len);
                tmp.erase(std::remove(tmp.begin(), tmp.end(), '\n'), tmp.end());
                current_command_output.append(tmp);
            }
            if(read_len == 0){
                captured_data++;
            }
            else{
                log_vector.push_back("stdout socket with -1 read condition.");
                log_vector.push_back("Error Number is " + std::to_string(errno));
                log_vector.push_back(strerror(errno));
            }

        }
        else{
            captured_data++;
        }

        if(fds[0].revents & POLLIN){
            int read_len = 0;
            
            while((read_len = read(fileno(perr), buffer.data(), 255)) > 0){
                std::string tmp; 
                tmp.append(buffer.data(), read_len);
                tmp.erase(std::remove(tmp.begin(), tmp.end(), '\n'), tmp.end());
                current_command_err.append(tmp);
            }
            if(read_len == 0){
                captured_data++;
            }
            else{
                log_vector.push_back("err socket with -1 read condition.");
                log_vector.push_back("Error Number is " + std::to_string(errno));
                log_vector.push_back(strerror(errno));
            }
        }
        else{
            captured_data++;
        }
    }
    
    
    fclose(perr);
    close(pfds[0]); close(pfds[1]);
    

    int return_code = pclose(pipe);
    current_command_result = return_code;
    log_vector.push_back("result returned with status code: " + std::to_string(current_command_result));
    log_vector.push_back("results where: \n" + current_command_output);
    log_vector.push_back("results err where: \n" + current_command_err);

    return;
}


void create_vm_function(struct_user_data &user_data, std::string create_vm_command, std::string tmp_cloud_config_text, std::string &current_error_message, std::string tmp_config_path, std::string vm_name){
        // run create vm, 
        // run virt-install and save results in config file...

       

        // tmp cloud config file.
        std::ofstream tmp_cloud_config(tmp_config_path);
        tmp_cloud_config << tmp_cloud_config_text;
        tmp_cloud_config.flush();
        tmp_cloud_config.close();

        // temp config path injection

        open_execute_read(create_vm_command);
        // if command failed then put error up and log result?
        if(current_command_result != 0){
            log_vector.push_back("Command ran: " + create_vm_command);
            log_vector.push_back("VM Create failed : " + current_command_output);
            bool_show_events_modal = true;
            // current_error_message = "VM Create failed: " + current_command_output;
        }
        // save current config into file.
        else{

            // get the uuid of the new vm, and store that and the name in the data construct.
            open_execute_read(std::vformat("virsh dominfo {} | grep UUID | awk '{{print $2}}'", std::make_format_args(vm_name)));
            if(current_command_result != 0){
                log_vector.push_back("Please contact admin, dangling VM left.");
                bool_show_events_modal = true;
            };
            

            user_data.virtual_machines.push_back({vm_name, current_command_output, false, ""});
            bool_update_components = true;

            // force save on creation
            save_structure(user_data);

        }
}

/*
    Edit VM Page UUID REFERENCE
*/

virtual_machine* current_vm;
std::string vm_edit_name;
std::string vm_edit_uuid;
bool vm_edit_autostart;



int main() {

    signal(SIGTERM, capture_flush_log);

    struct_general_config general_config = load_general_config("/etc", "capture.conf");
    struct_user_data user_data = load_structure(get_user_config_path(), "capture_config.json");

    // manage the screen object
    // ftxui::ScreenInteractive screen = ftxui::ScreenInteractive::TerminalOutput();
    ftxui::ScreenInteractive screen = ftxui::ScreenInteractive::Fullscreen();
    screen.TrackMouse(false);


    /*
        Get component data from the config file.
    */

    set_vm_cloud_ref_and_path(general_config);

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
        return ftxui::Button("Launch Local Shell", [&]{bool_launch_system_execution = true; launch_system_execution_path = "/bin/bash -c reset"; }, ftxui::ButtonOption::Ascii());
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

    const auto add_vm_button = ftxui::Button("Add VM", [&]{
        // update the ssh_key_names list
        for(auto key : user_data.public_keys){
            vm_ssh_key_names.push_back(key.name);
        }
        current_selection = 21;
    }, ftxui::ButtonOption::Ascii());
    const auto refresh_button = ftxui::Button("Refresh", [&]{bool_update_components = true;}, ftxui::ButtonOption::Ascii());
    
    ftxui::Component vm_edit_components(ftxui::Container::Vertical({})); // editable components list for the vms.
    
    const auto manage_vm_elements = [&]{
        return ftxui::flexbox({
            ftxui::flexbox({ 
                    add_vm_button->Render(),
                    ftxui::filler(),
                    refresh_button->Render(),
                    back_button->Render()
                },{
                    .direction = ftxui::FlexboxConfig::Direction::Row,
                    .align_items = ftxui::FlexboxConfig::AlignItems::Center,
                    .align_content = ftxui::FlexboxConfig::AlignContent::SpaceBetween,
                }) | ftxui::border | ftxui::size(ftxui::WIDTH, ftxui::GREATER_THAN, 90),
                ftxui::separatorEmpty(),
                vm_edit_components->Render()
        }, {
            .direction = ftxui::FlexboxConfig::Direction::Column,
            .align_items = ftxui::FlexboxConfig::AlignItems::FlexStart,
            .align_content = ftxui::FlexboxConfig::AlignContent::FlexStart
        }) | ftxui::size(ftxui::WIDTH, ftxui::GREATER_THAN, 90);
    };

    const auto manage_vm_components = ftxui::Container::Vertical({
        ftxui::Container::Horizontal({
        add_vm_button,
        refresh_button,
        back_button
        }),
        vm_edit_components
    });

    /*
        Manage Containers
    */
    const auto enable_containers = ftxui::Button("Enable Containers", [&]{
        open_execute_read("loginctl enable-linger");
        open_execute_read("systemctl --user enable podman.sock");
        open_execute_read("systemctl --user start podman.sock");
        user_data.containers_enabled = true;
        bool_update_components = true;
        return;
    }, ftxui::ButtonOption::Ascii());
    
    const auto disable_containers = ftxui::Button("Disable Containers", [&]{
        user_data.containers_enabled = false;

        bool_update_components = true;
        return;
    }, ftxui::ButtonOption::Ascii());
    
    const auto manage_containers = ftxui::Container::Vertical({});

    const auto manage_container_elements = [&]{
        return ftxui::flexbox({
            ftxui::flexbox({ 
                    enable_containers->Render(),
                    disable_containers->Render(),
                    ftxui::filler(),
                    back_button->Render()
                },{
                    .direction = ftxui::FlexboxConfig::Direction::Row,
                    .align_items = ftxui::FlexboxConfig::AlignItems::Center,
                    .align_content = ftxui::FlexboxConfig::AlignContent::SpaceBetween,
                }) | ftxui::border | ftxui::size(ftxui::WIDTH, ftxui::GREATER_THAN, 90),
                ftxui::separatorEmpty(),
                manage_containers->Render(),

        }, {
            .direction = ftxui::FlexboxConfig::Direction::Column,
            .align_items = ftxui::FlexboxConfig::AlignItems::FlexStart
        });
    };

    const auto manage_container_components = ftxui::Container::Vertical({
        ftxui::Container::Horizontal({
            enable_containers, 
            disable_containers, 
            back_button,
        }),
        manage_containers
    });

    const auto function_enable_containers = [&]{
        open_execute_read("podman info | grep sock | cut -d':' -f2 | xargs");
        std::string container_command = current_command_output;
        user_data.container_path = current_command_output;
    };

    const auto update_container_framework = [&](ftxui::Component manage_containers){
        log_vector.push_back("Container Framework Update");
        manage_containers->DetachAllChildren();
        // check if containers have been enabled
        if(user_data.containers_enabled){
            std::string formated_add = std::vformat(podman_add_remote, std::make_format_args(user_data.container_path));
            manage_containers->Add(ftxui::Renderer(ftxui::Container::Horizontal({}),
            [&]{
                return ftxui::vbox({
                    ftxui::text("Containers Enabled"),
                    ftxui::text("Socket path:"),
                    ftxui::text(user_data.container_path),
                    ftxui::text("This can be used in the podman system connection add command to add a container runtime."),
                    ftxui::text(formated_add)
                });
            }));
        }
        else{
            manage_containers->Add(ftxui::Renderer(
                ftxui::Container::Horizontal({
                }),
                [&]{
                    return ftxui::vbox({
                        ftxui::text("Container Management"),
                        ftxui::separatorEmpty(),
                        ftxui::text("Please enable container management")
                    });
                }
            ));
        }
        
        
        
        // for(auto it = user_data.virtual_machines.begin(); it != user_data.virtual_machines.end(); ++it){
            
        //     auto start_vm = ftxui::Button("Start", [it]{ open_execute_read("virsh start " + it->uuid); bool_update_components = true;}, ftxui::ButtonOption::Ascii());
        //     auto stop_vm = ftxui::Button("Stop", [it]{ open_execute_read("virsh shutdown " + it->uuid); bool_update_components = true;}, ftxui::ButtonOption::Ascii());
        //     auto edit_vm = ftxui::Button("Edit", [it]{ current_vm = it.base(); current_selection = 22;}, ftxui::ButtonOption::Ascii());
        //     auto console_vm = ftxui::Button("Console", [launch_console, it]{
        //         std::string cmd = std::vformat(launch_console, std::make_format_args(it->uuid));
        //         bool_launch_system_execution = true;
        //         launch_system_execution_path = cmd;
        //     }, ftxui::ButtonOption::Ascii());

        //     // get current state information, like if things are running or not.
            
        //     std::string formated_command = std::vformat(vm_get_current_state, std::make_format_args(it->uuid));
        //     open_execute_read(formated_command, false);
        //     std::string current_state = current_command_output;


        //     // text is contained with the button
        //     vm_edit_components->Add(ftxui::Renderer(
        //         ftxui::Container::Horizontal({
        //         console_vm, start_vm, stop_vm, edit_vm}), 
        //         [=]{
        //             if(bool_update_components) return ftxui::vbox({});

        //             return ftxui::vbox({
        //             ftxui::flexbox({
        //                 ftxui::text(it->name),
        //                 ftxui::filler(),
        //                 ftxui::text(current_state),
        //                 ftxui::filler(),
        //                 console_vm->Render(),
        //                 start_vm->Render(),
        //                 stop_vm->Render(),
        //                 edit_vm->Render()
        //                 }) | ftxui::size(ftxui::WIDTH, ftxui::GREATER_THAN, 90),
        //             ftxui::separatorDashed()
        //             });
                    
        //         })
        //     );
        // }
    };
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
    std::vector<std::string> network_options = {
        "Default - Default networking options.",
    "None - Only accessible through console.",
    "Shared Machine Connection - Connect to other vms and directly from the host.",
    "Network - Connect to Network Bridge - LAN ACCESS."
    };

    int network_options_selection = 0;

    const auto vm_name_input = ftxui::Input(&vm_name, "VM Name", { .multiline = false, });
    const auto vm_set_cpu = ftxui::Slider("Number of CPUs : ", &vm_cpu, 0, 40, 1);
    const auto vm_set_memory = ftxui::Slider("Memory : ", &vm_memory, 1024, 80*1024, 1024);
    const auto vm_disk_size_input = ftxui::Input(&primary_disk_size, "100", { .multiline = false, });
    const auto vm_networking = ftxui::Radiobox(network_options, &network_options_selection, ftxui::RadioboxOption::Simple());
    const auto vm_ssh_key_dropdown = ftxui::Dropdown(&vm_ssh_key_names, &vm_ssh_key_selector);
    const auto vm_cloud_config = ftxui::Dropdown(&vm_cloud_ref, &vm_cloud_selector);


    const auto create_vm = ftxui::Button("Create VM", [&]{
        

    
        std::string tmp_config_path("/tmp/cloud_config.tmp." + std::to_string(rand()));

        std::string vm_cloud_config = std::vformat(vm_cloud_config_template, std::make_format_args(user_data.username, user_data.public_keys.at(vm_ssh_key_selector).key, user_data.username));
        std::string vm_create_command_text = std::vformat(vm_create_template, std::make_format_args(vm_name, "Auto Created VM", vm_memory, vm_cpu, primary_disk_size, vm_cloud_image_path.at(vm_cloud_selector), tmp_config_path));

        /*
            Networking Details
        */
        if(network_options_selection == 1){
            vm_create_command_text += " --network none";
        }
        else if (network_options_selection == 2){
            vm_create_command_text += " --network bridge=virbr0";
        }
        else if (network_options_selection == 3){
            vm_create_command_text += " --network bridge=br0";
        }

        create_vm_function(user_data, vm_create_command_text, vm_cloud_config, current_error_message, tmp_config_path, vm_name);
        
        if(!bool_show_events_modal){
            current_selection = 2;
        }
        

    }, ftxui::ButtonOption::Ascii());

    const auto back_vm_page = ftxui::Button("Back", [&]{
        current_selection = 2;
    }, ftxui::ButtonOption::Ascii());

    const auto add_new_vm = [&]{
        return ftxui::flexbox({
            ftxui::flexbox({
                ftxui::text("Add New VM.") | ftxui::underlined
            },{
                .align_content = ftxui::FlexboxConfig::AlignContent::Center
            }),
            ftxui::separatorEmpty(),
            ftxui::text("VM Name:"),
            ftxui::flexbox({
                ftxui::filler(),
                vm_name_input->Render(),
                ftxui::filler(),
            }) | ftxui::size(ftxui::WIDTH, ftxui::GREATER_THAN, 50),

            ftxui::separatorEmpty(),
            ftxui::flexbox({
                ftxui::text("Number of vCPUs."),
                ftxui::filler(),
                ftxui::text(std::format("{} vCPUs ", vm_cpu)),
            }) | ftxui::size(ftxui::WIDTH, ftxui::GREATER_THAN, 50),
            ftxui::flexbox({
                ftxui::filler(),
                vm_set_cpu->Render() | ftxui::size(ftxui::WIDTH, ftxui::GREATER_THAN, 50),
                ftxui::filler(),    
            },{
            }) | ftxui::size(ftxui::WIDTH, ftxui::GREATER_THAN, 70),
            

            ftxui::separatorEmpty(),
            ftxui::flexbox({
                ftxui::text("Maximum memory of VM."),
                ftxui::filler(),
                ftxui::text(std::format("Memory {} MB", vm_memory)),
            }) | ftxui::size(ftxui::WIDTH, ftxui::GREATER_THAN, 50),
            ftxui::flexbox({
                ftxui::filler(),
                vm_set_memory->Render() | ftxui::size(ftxui::WIDTH, ftxui::GREATER_THAN, 50),
                ftxui::filler(),
                
            }) | ftxui::size(ftxui::WIDTH, ftxui::GREATER_THAN, 70),
            
            ftxui::separatorEmpty(),
            ftxui::text("Set the size of the primary disk size in GB. ( Do not add a unit. )"),
            ftxui::flexbox({
                ftxui::filler(),
                vm_disk_size_input->Render() | ftxui::size(ftxui::WIDTH, ftxui::GREATER_THAN, 50),
                ftxui::filler(),
            }) | ftxui::size(ftxui::WIDTH, ftxui::GREATER_THAN, 70),

            ftxui::separatorEmpty(),
            ftxui::text("Network options."),
            ftxui::flexbox({
                ftxui::filler(),
                vm_networking->Render(),
                ftxui::filler()
            }),

            ftxui::separatorEmpty(),
            ftxui::text("Select the base image for the VM."),
            ftxui::flexbox({
                vm_cloud_config->Render() | ftxui::size(ftxui::WIDTH, ftxui::GREATER_THAN, 25),
                ftxui::filler(),
            }) | ftxui::size(ftxui::WIDTH, ftxui::GREATER_THAN, 70),

            ftxui::separatorEmpty(),
            ftxui::text("Select the ssh key to attach."),
            ftxui::flexbox({
                vm_ssh_key_dropdown->Render() | ftxui::size(ftxui::WIDTH, ftxui::GREATER_THAN, 25),
                ftxui::filler(),
            }) | ftxui::size(ftxui::WIDTH, ftxui::GREATER_THAN, 70),

            ftxui::separatorEmpty(),
            ftxui::flexbox({
                ftxui::filler(),
                create_vm->Render(),
                ftxui::filler(),
                back_vm_page->Render(),
                ftxui::filler(),
            },{
                .align_content = ftxui::FlexboxConfig::AlignContent::SpaceBetween
            }) | ftxui::size(ftxui::WIDTH, ftxui::GREATER_THAN, 70)

        },{
                    .direction = ftxui::FlexboxConfig::Direction::Column,
                    .align_items = ftxui::FlexboxConfig::AlignItems::FlexStart,
                    .align_content = ftxui::FlexboxConfig::AlignContent::SpaceBetween,
        }) | ftxui::size(ftxui::WIDTH, ftxui::GREATER_THAN, 90);
    };

    const auto add_vm_components = ftxui::Container::Vertical({
        vm_name_input,
        vm_set_cpu,
        vm_set_memory,
        vm_disk_size_input,
        vm_networking,
        vm_cloud_config,
        vm_ssh_key_dropdown,
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

    std::string launch_console = "virsh console {}";

    const auto update_vm_components = [&](ftxui::Component vm_edit_components){
        vm_edit_components->DetachAllChildren();
        for(auto it = user_data.virtual_machines.begin(); it != user_data.virtual_machines.end(); ++it){
            
            auto start_vm = ftxui::Button("Start", [it]{ open_execute_read("virsh start " + it->uuid); bool_update_components = true;}, ftxui::ButtonOption::Ascii());
            auto stop_vm = ftxui::Button("Stop", [it]{ open_execute_read("virsh shutdown " + it->uuid); bool_update_components = true;}, ftxui::ButtonOption::Ascii());
            auto edit_vm = ftxui::Button("Edit", [it]{ current_vm = it.base(); current_selection = 22;}, ftxui::ButtonOption::Ascii());
            auto console_vm = ftxui::Button("Console", [launch_console, it]{
                std::string cmd = std::vformat(launch_console, std::make_format_args(it->uuid));
                bool_launch_system_execution = true;
                launch_system_execution_path = cmd;
            }, ftxui::ButtonOption::Ascii());

            // get current state information, like if things are running or not.
            
            std::string formated_command = std::vformat(vm_get_current_state, std::make_format_args(it->uuid));
            open_execute_read(formated_command, false);
            std::string current_state = current_command_output;


            // text is contained with the button
            vm_edit_components->Add(ftxui::Renderer(
                ftxui::Container::Horizontal({
                console_vm, start_vm, stop_vm, edit_vm}), 
                [=]{
                    if(bool_update_components) return ftxui::vbox({});

                    return ftxui::vbox({
                    ftxui::flexbox({
                        ftxui::text(it->name),
                        ftxui::filler(),
                        ftxui::text(current_state),
                        ftxui::filler(),
                        console_vm->Render(),
                        start_vm->Render(),
                        stop_vm->Render(),
                        edit_vm->Render()
                        }) | ftxui::size(ftxui::WIDTH, ftxui::GREATER_THAN, 90),
                    ftxui::separatorDashed()
                    });
                    
                })
            );
        }
    };

 
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
                    has_been_saved_since_last_update = false;
                }, ftxui::ButtonOption::Ascii());
            };
            
            
            auto button = get_removal_button();
            
            std::string local_name = it->name;
            std::string local_key = it->key;
         
            
         
            auto modal_text = [local_name, local_key, frame_position, button](){    
                return
                    ftxui::vbox({
                        ftxui::flexbox({
                            ftxui::text(local_name),
                            ftxui::filler(),
                            button->Render(),
                        },{
                            .direction = ftxui::FlexboxConfig::Direction::Row,
                            .align_content = ftxui::FlexboxConfig::AlignContent::SpaceBetween
                        })| ftxui::size(ftxui::WIDTH, ftxui::GREATER_THAN, 90),
                        ftxui::flexbox({
                            ftxui::text(local_key),
                            ftxui::filler(),
                        },{
                            .direction = ftxui::FlexboxConfig::Direction::Row,
                            .align_content = ftxui::FlexboxConfig::AlignContent::SpaceBetween
                        })| ftxui::size(ftxui::WIDTH, ftxui::GREATER_THAN, 90),
                        ftxui::separatorDashed(),
                    });
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

        has_been_saved_since_last_update = false;
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


    /*
        Manage VM ( EDIT BUTTON )
    */
    auto back_vm_manage = ftxui::Button("Back", [&]{current_selection = 2;}, ftxui::ButtonOption::Ascii());

    // uses current_vm->uuid for reference

    ftxui::Component vm_edit_start_button = ftxui::Button("Start", []{
        open_execute_read("virsh start " + current_vm->uuid);
    }, ftxui::ButtonOption::Ascii());
    ftxui::Component vm_edit_restart_button = ftxui::Button("Restart", []{
        open_execute_read("virsh restart " + current_vm->uuid);
    }, ftxui::ButtonOption::Ascii());
    ftxui::Component vm_edit_shutdown_button = ftxui::Button("Shutdown", []{
        open_execute_read("virsh shutdown " + current_vm->uuid);
    }, ftxui::ButtonOption::Ascii());
    ftxui::Component vm_edit_force_shutdown_button = ftxui::Button("Force Shutdown", []{
        open_execute_read("virsh destroy " + current_vm->uuid);
    }, ftxui::ButtonOption::Ascii());
    ftxui::Component vm_edit_force_restart_button = ftxui::Button("Force Restart", []{
        open_execute_read("virsh destroy " + current_vm->uuid);
        open_execute_read("virsh start " + current_vm->uuid);
    }, ftxui::ButtonOption::Ascii());

    // Console open
    ftxui::Component vm_edit_console = ftxui::Button("Console", [launch_console]{
                std::string cmd = std::vformat(launch_console, std::make_format_args(current_vm->uuid));
                bool_launch_system_execution = true;
                launch_system_execution_path = cmd;
            }, ftxui::ButtonOption::Ascii());

    // snapshot management -- early options only allow one snapshot? 
    ftxui::Component vm_edit_snapshot_create = ftxui::Button("Create", []{
        open_execute_read("virsh snapshot-create-as --domain " + current_vm->uuid + " --name " + current_vm->uuid + "_snapshot");
    }, ftxui::ButtonOption::Ascii());
    
    // in multi options, menu here to allow for selecting which one to revert or delete?
    ftxui::Component vm_edit_snapshot_revert = ftxui::Button("Revert", []{
        open_execute_read("virsh snapshot-revert --domain " + current_vm->uuid + " --snapshotname " + current_vm->uuid + "_snapshot");
    }, ftxui::ButtonOption::Ascii());
    ftxui::Component vm_edit_snapshot_delete = ftxui::Button("Delete", []{
        open_execute_read("virsh snapshot-delete --domain " + current_vm->uuid + " --snapshotname " + current_vm->uuid + "_snapshot");
    }, ftxui::ButtonOption::Ascii());
    
    // scrubbing options 
    ftxui::Component vm_edit_undefine = ftxui::Button("Remove Metadata", [&user_data]{
        open_execute_read("virsh undefine --domain " + current_vm->uuid);
        open_execute_read("virsh undefine --domain " + current_vm->uuid + " --remove-all-storage");
        if(current_command_result == 0){
            std::string uuid = current_vm->uuid;
            for (auto it = user_data.virtual_machines.begin(); it != user_data.virtual_machines.end();){
                if(it->uuid == uuid){
                    it = user_data.virtual_machines.erase(it);
                    break;
                }
                ++it;
            }
            current_selection = 2;
            save_structure(user_data);
        }

    }, ftxui::ButtonOption::Ascii());

    ftxui::Component vm_edit_undefine_disks = ftxui::Button("Remove Metadata and ALL DISKS", [&user_data]{
        open_execute_read("virsh undefine --domain " + current_vm->uuid + " --remove-all-storage");
        if(current_command_result == 0){
            std::string uuid = current_vm->uuid;
            for (auto it = user_data.virtual_machines.begin(); it != user_data.virtual_machines.end();){
                if(it->uuid == uuid){
                    it = user_data.virtual_machines.erase(it);
                    break;
                }
                ++it;
            }
            current_selection = 2;
            save_structure(user_data);
        }

    }, ftxui::ButtonOption::Ascii());
    

    // autostart options 

    ftxui::Component vm_edit_autostart_on = ftxui::Button("Enable Autostart", [&]{
        open_execute_read("virsh autostart --domain " + current_vm->uuid);
        current_vm->autostart = true;
    },ftxui::ButtonOption::Ascii()) | ftxui::Maybe([&]{return current_vm->autostart == false;});

    ftxui::Component vm_edit_autostart_off = ftxui::Button("Disable Autostart", [&]{
        open_execute_read("virsh autostart --disable --domain " + current_vm->uuid);
        current_vm->autostart = false;
    },ftxui::ButtonOption::Ascii()) | ftxui::Maybe([&]{return current_vm->autostart == true;});
    

    ftxui::Component current_vm_manage = ftxui::Container::Vertical({
        ftxui::Container::Horizontal({
            back_vm_manage
        }),
        ftxui::Container::Horizontal({
            vm_edit_start_button, 
            vm_edit_restart_button, 
            vm_edit_shutdown_button, 
            vm_edit_force_restart_button,
            vm_edit_force_shutdown_button,
            vm_edit_autostart_off,
            vm_edit_autostart_on
        }),
        ftxui::Container::Horizontal({
            vm_edit_console
        }),
        ftxui::Container::Horizontal({
            vm_edit_snapshot_create,
            vm_edit_snapshot_revert,
            vm_edit_snapshot_delete
        }),
        ftxui::Container::Horizontal({
            vm_edit_undefine,
            vm_edit_undefine_disks
        })
    });

    const auto current_vm_elements = [&]{

        open_execute_read(std::vformat("virsh domifaddr {} 2>/dev/null | tail -n2 | head -n 1 | awk '{{print $4}}'", std::make_format_args(current_vm->uuid)), true);
        std::string current_ip_addr = current_command_output;

        open_execute_read(std::vformat(vm_get_current_state, std::make_format_args(current_vm->uuid)));
        std::string current_vm_state = current_command_output;

        ftxui::Maybe(&current_vm->autostart);
        return ftxui::flexbox({
                ftxui::flexbox({
                    back_vm_manage->Render()
                },{
                    .direction = ftxui::FlexboxConfig::Direction::Row,
                    .align_content = ftxui::FlexboxConfig::AlignContent::FlexEnd
                }) | ftxui::size(ftxui::WIDTH, ftxui::GREATER_THAN, 90) | ftxui::border,
            ftxui::separatorEmpty(),
            ftxui::flexbox({
                ftxui::vbox({
                    ftxui::text(std::vformat("VM Name: {}", std::make_format_args(current_vm->name))),
                    ftxui::text(std::vformat("{}", std::make_format_args(current_vm->uuid))),
                    ftxui::text(std::vformat("IP: {}", std::make_format_args(current_ip_addr))),
                    ftxui::text(std::vformat("Current VM State: {} ", std::make_format_args(current_vm_state)))                
                }),
                ftxui::filler()
                
            },{
                .direction = ftxui::FlexboxConfig::Direction::Column,
                .align_items = ftxui::FlexboxConfig::AlignItems::FlexStart,
                .align_content = ftxui::FlexboxConfig::AlignContent::FlexStart
            }),
            ftxui::separatorEmpty(),
            ftxui::text("VM Management") | ftxui::underlined,
            ftxui::paragraph("Start, Restart and Stop VMs using the buttons below. Forcing either action may leave the VM in an unstable state.") | ftxui::size(ftxui::WIDTH, ftxui::LESS_THAN, 70),
            ftxui::separatorEmpty(),
            ftxui::flexbox({
                ftxui::filler(),
                vm_edit_start_button->Render(), 
                vm_edit_restart_button->Render(), 
                vm_edit_shutdown_button->Render(), 
                vm_edit_force_restart_button->Render() | ftxui::color(ftxui::Color::Orange1),
                vm_edit_force_shutdown_button->Render() | ftxui::color(ftxui::Color::Orange1),
                vm_edit_autostart_on->Render(),
                vm_edit_autostart_off->Render(),
                ftxui::filler()
            },{
                .direction = ftxui::FlexboxConfig::Direction::Row,
                .align_items = ftxui::FlexboxConfig::AlignItems::Center,
                .align_content = ftxui::FlexboxConfig::AlignContent::SpaceBetween
            }),
            ftxui::separatorEmpty(),
            ftxui::text("VM Interaction") | ftxui::underlined,
            ftxui::paragraph("Open the VM Console directly in order to interact.") | ftxui::size(ftxui::WIDTH, ftxui::LESS_THAN, 70),
            ftxui::separatorEmpty(),
            ftxui::flexbox({
                ftxui::filler(),
                vm_edit_console->Render(),
                ftxui::filler()
            },{
                .direction = ftxui::FlexboxConfig::Direction::Row,
                .align_items = ftxui::FlexboxConfig::AlignItems::Center,
                .align_content = ftxui::FlexboxConfig::AlignContent::SpaceBetween
            }),
            ftxui::separatorEmpty(),
            ftxui::text("Snapshot Management") | ftxui::underlined,
            ftxui::paragraph("Snapshots allow you to save the state of a VM, and later restore it to that previous state; discarding any changes made to the VM in the mean time.") | ftxui::size(ftxui::WIDTH, ftxui::LESS_THAN, 70),
            ftxui::separatorEmpty(),
            ftxui::flexbox({
                ftxui::filler(),
                vm_edit_snapshot_create->Render(),
                vm_edit_snapshot_revert->Render(),
                vm_edit_snapshot_delete->Render() | ftxui::color(ftxui::Color::Red),
                ftxui::filler()
            },{
                .direction = ftxui::FlexboxConfig::Direction::Row,
                .align_items = ftxui::FlexboxConfig::AlignItems::Center,
                .align_content = ftxui::FlexboxConfig::AlignContent::SpaceBetween
            }),
            ftxui::separatorEmpty(),
            ftxui::text("VM Deletion") | ftxui::underlined,
            ftxui::paragraph("Choosing either of the options will schedule the vm for deletion, either immediately or upon shutdown. Choosing to remove the disks will delete all disks associated with the VM.") | ftxui::size(ftxui::WIDTH, ftxui::LESS_THAN, 70),
            ftxui::separatorEmpty(),
            ftxui::flexbox({
                ftxui::filler(),
                vm_edit_undefine->Render() | ftxui::color(ftxui::Color::Red),
                vm_edit_undefine_disks->Render() | ftxui::color(ftxui::Color::Red),
                ftxui::filler()
            },{
                .direction = ftxui::FlexboxConfig::Direction::Row,
                .align_items = ftxui::FlexboxConfig::AlignItems::Center,
                .align_content = ftxui::FlexboxConfig::AlignContent::SpaceBetween
            })
        }, 
        {
            .direction = ftxui::FlexboxConfig::Direction::Column,
            .align_items = ftxui::FlexboxConfig::AlignItems::Stretch,
            .align_content = ftxui::FlexboxConfig::AlignContent::FlexStart
        }) | ftxui::size(ftxui::WIDTH, ftxui::GREATER_THAN, 90) | ftxui::size(ftxui::HEIGHT, ftxui::GREATER_THAN, 90);
    };

    // current vm management
    current_vm_manage_base = ftxui::Renderer(current_vm_manage, current_vm_elements);

    // ftxui::Render(screen, document);
    
    main_menu_base = ftxui::Renderer(main_menu_components, main_menu_elements);

    // user edit component

    user_edit_base = ftxui::Renderer(user_edit_components, get_user_edit);

    // manage containers 

    const auto container_manage_base = ftxui::Renderer(manage_container_components, manage_container_elements);

    // vm management 

    const auto vm_management_base = ftxui::Renderer(manage_vm_components, manage_vm_elements);

    // vm add modal

    const auto vm_add_modal_base = ftxui::Renderer(add_vm_components, add_new_vm);

    // ssh key edit component

    update_ssh_key_dynamic();

    // inital run of vm components 

    update_vm_components(vm_edit_components);

    // Manage Containers

    auto show_events_modal = [&]{bool_show_events_modal = true; };
    auto exit_events_modal = [&]{bool_show_events_modal = false; };

    events_modal_base = events_modal(current_error_message, exit_events_modal);

    const auto generate_primary_component = [&]{

        auto test = ftxui::Container::Vertical({
                ftxui::Maybe(main_menu_base, [&]{ return current_selection == -1;}),
                ftxui::Maybe(user_edit_base, [&]{ return current_selection == 0;}),
                ftxui::Maybe(ssh_key_edit_base, [&]{ return current_selection == 1;}),
                ftxui::Maybe(vm_management_base, [&]{return current_selection == 2;}),
                ftxui::Maybe(vm_add_modal_base, [&]{return current_selection == 21;}),
                ftxui::Maybe(current_vm_manage_base, [&]{return current_selection == 22;}),
                ftxui::Maybe(container_manage_base, [&]{return current_selection == 3;})
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
        
        if(bool_update_components){
            get_ssh_edit_components(ssh_key_dynamic_list);
            update_vm_components(vm_edit_components);
            update_container_framework(manage_containers);
            bool_update_components = false;
        }
        if(bool_launch_system_execution){
            screen.Print();
            std::cout << screen.ResetPosition(true) << std::flush;
            std::cout << "If console appears stuck, pressing any keys will allow it to continue \n" << std::flush;
            log_vector.push_back("Executing console: " + launch_system_execution_path);
            
            screen.pause_interact.lock();
            system(launch_system_execution_path.c_str());
            bool_launch_system_execution = false;
            screen.pause_interact.unlock();
            log_vector.push_back("Finished console");

            screen.RequestAnimationFrame();
            // std::cout << screen.ToString() << screen.ResetPosition() << std::flush;
            // std::cout << "Press any key to refresh the screen." << std::flush;
        }
        
        loop.RunOnceBlocking();
        /*
            Call out of line updating functions?
        */

    }

    for(int i = 0; i < 100; i++){
        std::cout << "\n";
    }
    // for(auto str : log_vector){
    //     std::cout << "\n";
    //     int count = 0;
    //     for(auto i : str){
    //         count++;
    //         std::cout << i;
    //         if(count % 150 == 0){
    //             std::cout << "\n";
    //         }
    //     }
    // }

    flush_log();

    return 0;
}