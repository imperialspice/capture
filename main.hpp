
#include "rfl.hpp"
#include "rfl/json.hpp"
#include <vector>
#include <string>


/*
    Config variables
*/


/*
    Log capturing
*/
std::vector<std::string> log_vector;


/*
 Cloud Selector

*/


/*
    Structures
*/

struct struct_general_config{
    int version;
    std::string default_image_path;
    bool search_directory;
};


struct ssh_key{
    std::string name;
    std::string key;
};

struct virtual_machine{
    std::string name;
    std::string uuid;
    bool autostart;
    std::string ip;
};

struct struct_user_data {
    std::string name;
    std::string vm_ssh_address;
    std::string username;
    std::vector<virtual_machine> virtual_machines;
    std::vector<ssh_key> public_keys;
    std::vector<std::pair<std::string, std::string>> cloud_images;
};



std::string_view get_user_config_path();
void save_structure(struct_user_data &user_data);
struct_user_data load_structure();