#include "starfox/vr/game_frame_driver.hpp"
#include "starfox/vr/startup_menu.hpp"
#include "starfox/assets/embedded.hpp"
#include "starfox/assets/runtime_bundle.hpp"
#include <fstream>
#include <iostream>
#include <iterator>
#include <stdexcept>

namespace {
void require(bool value,const char* message) {if(!value) throw std::runtime_error(message);}
void check_cartridge(const std::vector<uint8_t>& bytes,const std::string& symbol_text) {
    using namespace starfox;
    const assets::RomImage rom(bytes);
    const auto symbols=assets::SymbolMap::parse(symbol_text);
    for(bool fixed:{false,true}) for(uint8_t type=0;type<4;++type) {
        simulation::GameSimulation game(rom,symbols,"LEVEL1_1",{},true);
        game.set_timing_mode(simulation::TimingMode::unlocked_20_fps);
        game.map().write_native_byte(symbols.find("C_TYPE").at(0),type);
        vr::GameSceneHistory history(game,rom,symbols);
        vr::GameFrameDriver driver(game,[](auto,auto) {return std::array<uint8_t,4>{};},&history);
        const auto held=[&] {
            return input::ButtonMask((game.map().read_native_byte(symbols.find("CONT0").at(0))<<8)
                |game.map().read_native_byte(symbols.find("CONTL0").at(0)));
        };
        (void)driver.advance(0,{},true,{},fixed);
        vr::VrControls controls;controls.fire=true;controls.steer.y=1;
        (void)driver.advance(50'000'000,controls,true,{},fixed);
        const auto fire=(fixed || !(type&1))?input::y:input::b;
        require((held()&(input::b|input::y))==fire,"Quest A does not reach the native fire bit");
        require((held()&(input::up|input::down))==((type&2)?input::down:input::up),"Vertical inversion changed");
        (void)driver.advance(100'000'000,{},true,{},fixed);
        require(!(held()&(input::b|input::y)),"Released fire remains held");
        controls={};controls.brake=true;
        (void)driver.advance(150'000'000,controls,true,{},fixed);
        const auto brake=(fixed || !(type&1))?input::b:input::y;
        require((held()&(input::b|input::y))==brake,"Quest Y does not reach the native brake bit");
        controls.fire=true;
        (void)driver.advance(200'000'000,controls,true,{},fixed);
        require((held()&(input::b|input::y))==(input::b|input::y),"Simultaneous actions changed");
    }
}
}
int main(int argc,char** argv) try {
    using namespace starfox;
    constexpr input::ButtonMask other=input::a|input::x|input::start|input::select|input::up|input::left_shoulder;
    for(uint8_t type=0;type<4;++type) {
        input::TickInput edges{other,input::y,input::b};
        const auto result=vr::fixed_face_button_input(edges,type);
        require(result.held==other,"Unrelated buttons changed");
        require(result.pressed==((type&1)?input::b:input::y),"Pressed edge not translated");
        require(result.released==((type&1)?input::y:input::b),"Released edge not translated");
    }
    vr::StartupMenu menu;menu.fixed_face_buttons=true;menu.swap_face_buttons=true;
    vr::VrControls controls;controls.fire=true;
    require(menu.gameplay_controls(controls).fire && !menu.gameplay_controls(controls).boost,"Saved swap changed Quest A");
    controls={};controls.brake=true;
    require(menu.gameplay_controls(controls).brake && !menu.gameplay_controls(controls).bomb,"Saved swap changed Quest Y");
    menu.page=vr::StartupMenu::Page::options;menu.selection=2;menu.sample({},true);
    controls={};controls.fire=true;menu.sample(controls,true);
    require(menu.swap_face_buttons && menu.labels()[2]=="A: FIRE / Y: BRAKE","Fixed Quest menu row is not stable");
    require(argc==2,"usage: starfox_vr_face_input_check Starfox-Assets.BIN");
    std::ifstream input(argv[1],std::ios::binary);
    require(bool(input),"Cannot open test bundle");
    const std::vector<uint8_t> bytes{std::istreambuf_iterator<char>(input),std::istreambuf_iterator<char>()};
    const auto bundle=assets::decode_runtime_bundle(bytes,assets::runtime_companion_manifest(assets::embedded_asset));
    check_cartridge(bundle.original_rom,bundle.original_symbols);
    check_cartridge(bundle.starfox_ex_rom,bundle.starfox_ex_symbols);
    std::cout<<"Quest face buttons passed: Original + EX, all four control types, A fire/Y brake, edges, saved swap and vertical inversion\n";
    return 0;
} catch(const std::exception& error) {std::cerr<<error.what()<<'\n';return 1;}
