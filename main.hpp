
#include <vector>
#include <string>


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
    std::vector<std::string> virtual_machines;
    std::vector<ssh_key> public_keys;
    std::vector<std::pair<std::string, std::string>> cloud_images;
};


std::string_view get_user_config_path();
void save_structure(struct_user_data &user_data);
struct_user_data load_structure();