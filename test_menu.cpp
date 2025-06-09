// Copyright 2020 Arthur Sonzogni. All rights reserved.
// Use of this source code is governed by the MIT license that can be found in
// the LICENSE file.
#include <ftxui/component/component_base.hpp>
#include <ftxui/dom/elements.hpp>
#include <functional>  // for function
#include <iostream>  // for basic_ostream::operator<<, operator<<, endl, basic_ostream, basic_ostream<>::__ostream_type, cout, ostream
#include <string>    // for string, basic_string, allocator
#include <vector>    // for vector

#include "ftxui/component/captured_mouse.hpp"
#include "ftxui/component/component.hpp"           // for Menu
#include "ftxui/component/component_options.hpp"   // for MenuOption
#include "ftxui/component/screen_interactive.hpp"  // for ScreenInteractive
 
int main() {
  using namespace ftxui;
  auto screen = ScreenInteractive::Fullscreen();
 
  std::vector<std::string> entries = {
      "entry 1",
      "entry 2",
      "entry 3",
  };
  int selected = 0;
 
  MenuOption option{
    .entries=&entries,
    .selected=&selected,
    .on_enter=screen.ExitLoopClosure()
  };
 
  auto _main_menu = Menu(option);

  auto list = Container::Vertical({
    _main_menu,
  });
 
  Element test = ftxui::vflow({
                    ftxui::text("The current active virtual machine is accessable at:"),
                    _main_menu->Render()
                });

auto mixed_component = ftxui::Renderer(_main_menu, [&]{return  ftxui::vflow({
                    ftxui::text("The current active virtual machine is accessable at:"),
                    _main_menu->Render()
                });
});


//   screen.Loop(mixed_component);
screen.Loop(mixed_component);
 
  std::cout << "Selected element = " << selected << std::endl;

// auto screen = ScreenInteractive::TerminalOutput();
// std::string label = "Click to quit";
// auto button = Button(&label, screen.ExitLoopClosure());
// auto renderer = Renderer(button, [&] {
//   return hbox({
//     text("A button:"),
//     button->Render(),
//   });
// });
// screen.Loop(renderer);

//   std::vector<std::string> left_menu_entries = {
//       "0%", "10%", "20%", "30%", "40%", "50%", "60%", "70%", "80%", "90%",
//   };
//   std::vector<std::string> right_menu_entries = {
//       "0%", "1%", "2%", "3%", "4%", "5%", "6%", "7%", "8%", "9%", "10%",
//   };
 
//   auto menu_option = MenuOption();
//   menu_option.on_enter = screen.ExitLoopClosure();
 
//   int left_menu_selected = 0;
//   int right_menu_selected = 0;
//   Component left_menu_ =
//       Menu(&left_menu_entries, &left_menu_selected, menu_option);
//   Component right_menu_ =
//       Menu(&right_menu_entries, &right_menu_selected, menu_option);
 
//   Component container = Container::Vertical({
//       left_menu_,
//       right_menu_,
//   });
 
//   auto renderer = Renderer(container, [&] {
//     int sum = left_menu_selected * 10 + right_menu_selected;
//     return vbox({
//                // -------- Top panel --------------
//                hbox({
//                    // -------- Left Menu --------------
//                    vbox({
//                        hcenter(bold(text("Percentage by 10%"))),
//                        separator(),
//                        left_menu_->Render(),
//                    }),
//                    separator(),
//                    // -------- Right Menu --------------
//                    vbox({
//                        hcenter(bold(text("Percentage by 1%"))),
//                        separator(),
//                        right_menu_->Render(),
//                    }),
//                    separator(),
//                }),
//                separator(),
//                // -------- Bottom panel --------------
//                vbox({
//                    hbox({
//                        text(" gauge : "),
//                        gauge(sum / 100.0),
//                    }),
//                    hbox({
//                        text("  text : "),
//                        text(std::to_string(sum) + " %"),
//                    }),
//                }),
//            }) |
//            border;
//   });
 
//   screen.Loop(renderer);

}