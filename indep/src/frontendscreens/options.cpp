#include "FMenus.hpp"
#include "Flags.hpp"
#include "Language.hpp"
#include "FEAnim.hpp"
#include "Fred.hpp"
#include "NativeAudioProfileValidation.hpp"
#include "CarLoader.hpp"
#include "Timer.hpp"
#include <tuple>
#include "bware/inc/bWare.hpp"
#include "bware/inc/bMath.hpp"

namespace hp2::frontend {

FlailerSlideOnScreen::FlailerSlideOnScreen(FlailerUiState& state,std::uint32_t screen,
    std::uint32_t anchor,std::uint32_t group,std::uint32_t visibilityScreen)
    : ui_(state),screen_(screen),anchor_(anchor),group_(group),visibilityScreen_(visibilityScreen)
{
    Require(ui_.resources && ui_.render && ui_.clock,"Options slide has no actual UI resource/render/clock owners");
    position.duration=position.threshold=1.0f;
    position.continuous=1;
    if (const auto* object=ui_.resources->FindObject(screen_,anchor_))
        ui_.resources->GetTopLeft(*object,savedX_,savedY_,ui_.language);
    Appear();
}

FlailerSlideOnScreen::~FlailerSlideOnScreen()
{
    Disappear();
}

void FlailerSlideOnScreen::Tick()
{
    tCubic1DUpdate(&position,ui_.clock->realTimeElapsed,0.0f,0.0f);
    if (ui_.resources->FindObject(screen_,anchor_))
        ui_.resources->MoveGroupTo(screen_,group_,anchor_,
            static_cast<float>(convertFloatToInt32(position.value)),
            static_cast<float>(convertFloatToInt32(savedY_)));
}

void FlailerSlideOnScreen::Disappear()
{
    ui_.resources->MoveGroupTo(screen_,group_,anchor_,
        static_cast<float>(convertFloatToInt32(savedX_)),
        static_cast<float>(convertFloatToInt32(savedY_)));
    ui_.render->SetScreenVisibility(*ui_.resources,visibilityScreen_,1);
}

void FlailerSlideOnScreen::Appear()
{
    position.desiredValue=ui_.animationSettings.screenSlideStartPosition;
    position.duration=0.5f;position.continuous=0;
    if (position.value!=position.desiredValue)position.mode=2;
    position.mode=0;position.value=position.desiredValue;
    position.derivative=position.desiredDerivative;
    position.desiredValue=savedX_;
    if (position.value!=position.desiredValue)position.mode=2;
    ui_.render->SetScreenVisibility(*ui_.resources,visibilityScreen_,0);
    Tick();
}


namespace {
constexpr std::uint32_t OptionsGroups[]={0x6e34e350u,0x708a0bf1u,0x72df3492u,0x75345d33u,
    0x778985d4u,0x79deae75u,0x7c33d716u,0x7e88ffb7u};
MainMenuScreenServices* optionsServices{};
std::unique_ptr<OptionsScreenBase> optionsOwner;
void ClearLanguageOptionsOwner();

float OptionsLeft(FlailerUiState& ui,std::uint32_t screen)
{
    auto* object=ui.resources->FindObject(screen,OptionsGroups[0]);
    Require(object,"Options slide has no authored first menu group");
    float x,y;ui.resources->GetTopLeft(*object,x,y,ui.language);return x;
}

// Full original DifferentData1EE688. This is comparison of actual values,
// never a native object's padding, console pointer image or second owner.
bool DifferentData(const void* first,const void* second,std::int32_t count)
{
    auto* a=static_cast<const std::uint8_t*>(first);
    auto* b=static_cast<const std::uint8_t*>(second);
    for (;count>0;--count,++a,++b)if (*a!=*b)return true;
    return false;
}

bool DifferentPlayerSettings(const PlayerSettings& a,const PlayerSettings& b)
{
    // The original snapshot compares its complete 48-byte authored settings
    // record. Native field arrangement differs: compare every logical field
    // and all twelve name bytes, deliberately excluding native padding.
    return std::tie(a.inputProfile,a.authoredOption,a.initialVehicleMode,a.gearInputMode,
        a.cameraView,a.alternateCamera,a.splitTimeMode,a.authoredTimeDisplayOption,
        a.cameraSpring,a.lookback,a.hudFlags,a.bestTotalTimeTicks,a.bestLapTimeTicks,
        a.hudHotkey,a.scoring)!=
        std::tie(b.inputProfile,b.authoredOption,b.initialVehicleMode,b.gearInputMode,
        b.cameraView,b.alternateCamera,b.splitTimeMode,b.authoredTimeDisplayOption,
        b.cameraSpring,b.lookback,b.hudFlags,b.bestTotalTimeTicks,b.bestLapTimeTicks,
        b.hudHotkey,b.scoring) || DifferentData(a.playerName,b.playerName,PLAYER_NAME_CAPACITY);
}
}

struct DisplayOptionsChangeManager::Storage {
    std::array<std::int32_t,2> calibration{};
    GlobalDisplaySettings display{};
    std::array<PlayerSettings,2> players{};
};
DisplayOptionsChangeManager::DisplayOptionsChangeManager():storage_(std::make_unique<Storage>()) {}
DisplayOptionsChangeManager::~DisplayOptionsChangeManager()=default;
DisplayOptionsChangeManager gDisplayOptionsChangeManager;

void DisplayOptionsChangeManager::Start(const FrontendPlayerManager& frontend)
{
    // PAL1EE6C0 snapshots FRED+144 calibration, +14C display and both real
    // profile settings. This source-owned snapshot is intentionally separate
    // from the live settings; it neither applies nor fabricates a change.
    Require(frontend.players[0] && frontend.players[1],"Display change snapshot requires both actual profiles");
    storage_->calibration=frontend.displayCalibration;
    storage_->display=frontend.displaySettings;
    storage_->players[0]=frontend.players[0]->settings;
    storage_->players[1]=frontend.players[1]->settings;
}

bool DisplayOptionsChangeManager::Different(const FrontendPlayerManager& frontend) const
{
    Require(frontend.players[0] && frontend.players[1],"Display change comparison requires both actual profiles");
    if (DifferentData(storage_->calibration.data(),frontend.displayCalibration.data(),8))return true;
    static_assert(sizeof(GlobalDisplaySettings)==8,"All eight original display choices must remain present");
    if (DifferentData(&storage_->display,&frontend.displaySettings,8))return true;
    if (DifferentPlayerSettings(storage_->players[0],frontend.players[0]->settings))return true;
    return DifferentPlayerSettings(storage_->players[1],frontend.players[1]->settings);
}

OptionsMenuState::OptionsMenuState(MainMenuScreenServices& bindings,std::uint32_t key)
    : services(bindings),screen(key),slide(*bindings.ui,key,OptionsLeft(*bindings.ui,key)),
      highlight(*bindings.ui,key,0xab671c9au,bindings.ui->animationSettings.highlightColor),
      selectionChange(*bindings.ui,key,0x1042eac3u,-1),selection(std::make_unique<FlailerSelectionSet>()) {}

MainMenuScreenServices& OptionsScreenBase::BoundServices()
{
    Require(optionsServices,"Options screen has no canonical service bindings");
    return *optionsServices;
}
void OptionsScreenBase::Install(std::unique_ptr<OptionsScreenBase> instance)
{
    Require(instance && !optionsOwner,"Options screen duplicates the original live instance");
    optionsOwner=std::move(instance);
}

OptionsScreenBase::OptionsScreenBase(MainMenuScreenServices& services,std::uint32_t screen)
    : state_(std::make_unique<OptionsMenuState>(services,screen))
{
    // PAL1F4C98; the eight groups are read from final2BA430.
    state_->selection->showOtherItems=1;
    state_->selection->AddItems(*services.ui->resources,screen,OptionsGroups,8);
    services.screens->StartMovie("MOVIES\\options_movie_new.AVI");
}
OptionsScreenBase::~OptionsScreenBase()=default;

void OptionsScreenBase::HandleHighlight(ScreenMessage,ScreenCallbackData& data,std::uint32_t group)
{
    if (!group && data.key>=0x52e039f3u && data.key<=0x52e039f9u)
        group=OptionsGroups[data.key-0x52e039f3u];
    state_->selection->SetCurrentItem(group);
    auto& ui=*state_->services.ui;
    std::int32_t left,top,right,bottom;
    Require(ui.resources->GetBoundingBox(state_->screen,group,left,top,right,bottom,ui.language),
        "Options highlight has no authored bounding rectangle");
    // The Options lower rail is at bottom, unlike the MainMenu/Cops -3.
    if (auto* lower=ui.resources->FindObject(state_->screen,0x32c91daeu))
        lower->SetTopLeftY(static_cast<float>(bottom));
    if (auto* upper=ui.resources->FindObject(state_->screen,0xc671680cu))
        upper->SetTopLeftY(static_cast<float>(top-6));
    state_->highlight.SetCurrentMenuItem(data.key);
}

void OptionsScreenBase::FirstCrackAtMessage(ScreenMessage message,ScreenCallbackData& data)
{
    if (message==ScreenMessage::Highlight)HandleHighlight(message,data,0);
    else if (message==ScreenMessage::Tick) {
        state_->slide.Animate();state_->highlight.Animate();state_->selectionChange.Animate();
    }
    if (state_->exit) {state_->exit->HandleMessages(message,data);return;}
    HandleEvent(message,data);
}

void OptionsScreenBase::BaseCallback(ScreenMessage message,ScreenCallbackData* data)
{
    // PAL1F5060's one actual OptionsScreenBase::pInstance owner, shared by
    // Options family implementations when they are implemented and bound.
    if (message==ScreenMessage::ScreenDeactivate) {optionsOwner.reset();return;}
    Require(optionsOwner && data,"Options callback arrived without its actual owner/data");
    optionsOwner->FirstCrackAtMessage(message,*data);
}


namespace {
using Translation=std::array<const char*,6>;
enum class PcOption {
    Resolution,Window,Vsync,FrameLimit,Distance,Hud,RenderScale,Antialiasing,Anisotropy,Fps,
    ResolutionPage,Aspect,Scaling,Detail,Reflections,Advanced,AntialiasPage,Quality,Brightness,Contrast,Gamma,PictureDefaults,
    Player,Keyboard,Gamepad,Deadzone,Steering,IconStyle,Layouts,PadDefaults,Player2Pad,KeyboardDefaults,InputMode,
    AdaptiveTriggers,PoliceRadioSpeaker,PoliceRadioVolume,
    Profile,NewProfile,SaveLoad,Name,
    Audio,Controls,Graphics,Profiles,Languages,Credits,
    English,French,German,Italian,Spanish,Swedish,ZoneFreeze,ZoneZoom,
    Accelerate,Brake,SteerLeft,SteerRight,Handbrake,Camera,ShiftUp,ShiftDown,LookBack,Horn,Nitro,Reset,HudToggle
};
struct PcOptionLabel {
    PcOption option;
    Translation text;
};
constexpr PcOptionLabel Labels[]{
    {PcOption::Resolution,{"Resolution","Resolution","Aufloesung","Risoluzione","Resolucion","Upplosning"}},
    {PcOption::Window,{"Window","Mode d'affichage","Bildschirmmodus","Modalita schermo","Modo de pantalla","Skarmtyp"}},
    {PcOption::Vsync,{"VSync","VSync","VSync","VSync","VSync","VSync"}},
    {PcOption::FrameLimit,{"Frame limit","Limite d'IPS","Bildratenlimit","Limite FPS","Limite de FPS","Bildrutetak"}},
    {PcOption::Distance,{"Draw distance","Distance d'affichage","Sichtweite","Distanza visiva","Distancia visual","Siktavstand"}},

    {PcOption::ResolutionPage,{"Resolution","Resolution","Aufloesung","Risoluzione","Resolucion","Upplosning"}},
    {PcOption::Aspect,{"Aspect ratio","Format d'image","Seitenverhaeltnis","Proporzioni","Relacion de aspecto","Bildformat"}},
    {PcOption::Scaling,{"Scaling","Mise a l'echelle","Skalierung","Ridimensionamento","Escalado","Skalning"}},
    {PcOption::Detail,{"Level of detail","Niveau de detail","Detailgrad","Livello dettaglio","Nivel de detalle","Detaljniva"}},
    {PcOption::Reflections,{"Reflections","Reflets","Reflexionen","Riflessi","Reflejos","Reflektioner"}},
    {PcOption::Advanced,{"Advanced","Avance","Erweitert","Avanzate","Avanzado","Avancerat"}},
    {PcOption::AntialiasPage,{"Antialiasing","Anticrenelage","Kantenglaettung","Antialiasing","Suavizado","Kantutjamning"}},
    {PcOption::Quality,{"Quality","Qualite","Qualitaet","Qualita","Calidad","Kvalitet"}},
    {PcOption::Brightness,{"Brightness","Luminosite","Helligkeit","Luminosita","Brillo","Ljusstyrka"}},
    {PcOption::Contrast,{"Contrast","Contraste","Kontrast","Contrasto","Contraste","Kontrast"}},
    {PcOption::Gamma,{"Gamma","Gamma","Gamma","Gamma","Gamma","Gamma"}},
    {PcOption::PictureDefaults,{"Defaults","Valeurs par defaut","Standard","Predefiniti","Predeterminados","Standard"}},
    {PcOption::Audio,{"Audio","Audio","Audio","Audio","Audio","Ljud"}},
    {PcOption::Controls,{"Controls","Commandes","Steuerung","Comandi","Controles","Kontroller"}},
    {PcOption::Graphics,{"Graphics","Graphismes","Grafik","Grafica","Graficos","Grafik"}},
    {PcOption::Profiles,{"Profiles","Profils","Profile","Profili","Perfiles","Profiler"}},
    {PcOption::Languages,{"Languages","Langues","Sprachen","Lingue","Idiomas","Sprak"}},
    {PcOption::Credits,{"Credits","Credits","Mitwirkende","Crediti","Creditos","Medverkande"}},
    {PcOption::Hud,{"HUD","Interface","Instrumente","Indicatori","Indicadores","Instrument"}},
    {PcOption::RenderScale,{"Render scale","Echelle de rendu","Renderskalierung","Scala rendering","Escala de render","Renderingsskala"}},
    {PcOption::Antialiasing,{"Anti-aliasing","Anticrenelage","Kantenglaettung","Antialiasing","Suavizado","Kantutjamning"}},
    {PcOption::Anisotropy,{"Anisotropic filtering","Anisotropie","Anisotropie","Anisotropia","Anisotropia","Anisotropi"}},
    {PcOption::Fps,{"Show FPS","Afficher les IPS","FPS anzeigen","Mostra FPS","Mostrar FPS","Visa FPS"}},
    {PcOption::Player,{"Player","Joueur","Spieler","Giocatore","Jugador","Spelare"}},
    {PcOption::Keyboard,{"Keyboard","Clavier","Tastatur","Tastiera","Teclado","Tangentbord"}},
    {PcOption::Gamepad,{"Gamepad","Manette","Gamepad","Gamepad","Mando","Handkontroll"}},
    {PcOption::AdaptiveTriggers,{"Adaptive Triggers","Gachettes adaptatives","Adaptive Trigger",
        "Grilletti adattivi","Gatillos adaptativos","Adaptiva avtryckare"}},
    {PcOption::PoliceRadioSpeaker,{"Police Radio Speaker","Radio sur la manette","Polizeifunk am Gamepad",
        "Radio sul controller","Radio en el mando","Polisradio i handkontroll"}},
    {PcOption::PoliceRadioVolume,{"Speaker Volume","Volume de la manette","Lautsprecherlautstaerke",
        "Volume controller","Volumen del mando","Handkontrollens volym"}},
    {PcOption::Deadzone,{"Steering deadzone","Zone morte direction","Lenktotzone","Zona morta sterzo","Zona muerta de giro","Styrningens dodzon"}},
    {PcOption::Steering,{"Steering","Direction","Lenkung","Sterzo","Direccion","Styrning"}},
    {PcOption::IconStyle,{"Button icons","Icones","Tastensymbole","Icone","Iconos","Knappikoner"}},
    {PcOption::Layouts,{"Layouts","Disposition","Belegungen","Configurazioni","Esquemas","Layouter"}},
    {PcOption::PadDefaults,{"Defaults","Valeurs par defaut","Standard","Predefiniti","Predeterminados","Standard"}},
    {PcOption::KeyboardDefaults,{"Reset to default","Reinitialiser","Zuruecksetzen","Ripristina","Restaurar","Aterstall"}},
    {PcOption::Player2Pad,{"Gamepad","Manette","Gamepad","Gamepad","Mando","Handkontroll"}},
    {PcOption::InputMode,{"Input device","Peripherique","Eingabegeraet","Dispositivo","Dispositivo","Inmatningsenhet"}},
    {PcOption::NewProfile,{"New profile","Nouveau profil","Neues Profil","Nuovo profilo","Nuevo perfil","Ny profil"}},
    {PcOption::SaveLoad,{"Save / load progress","Sauvegarder / charger","Speichern / Laden","Salva / carica","Guardar / cargar","Spara / las in"}},
    {PcOption::Name,{"Player names","Noms des joueurs","Spielernamen","Nomi giocatori","Nombres","Spelarnamn"}},
    {PcOption::English,{"English","English","English","English","English","English"}},
    {PcOption::French,{"Francais","Francais","Francais","Francais","Francais","Francais"}},
    {PcOption::German,{"Deutsch","Deutsch","Deutsch","Deutsch","Deutsch","Deutsch"}},
    {PcOption::Italian,{"Italiano","Italiano","Italiano","Italiano","Italiano","Italiano"}},
    {PcOption::Spanish,{"Espanol","Espanol","Espanol","Espanol","Espanol","Espanol"}},
    {PcOption::Swedish,{"Svenska","Svenska","Svenska","Svenska","Svenska","Svenska"}},
    {PcOption::ZoneFreeze,{"Zone Freeze","Zone figee","Zone einfrieren","Blocca zona","Pausa de zona","Frys zon"}},
    {PcOption::ZoneZoom,{"Zone Zoom","Zoom de zone","Zonenzoom","Zoom zona","Zoom de zona","Zonzoom"}},
    {PcOption::Accelerate,{"Accelerate","Accelerer","Gas geben","Accelera","Acelerar","Gasa"}},
    {PcOption::Brake,{"Brake / reverse","Freiner / reculer","Bremse / Rueckwaerts","Frena / retromarcia","Freno / marcha atras","Bromsa / backa"}},
    {PcOption::SteerLeft,{"Steer left","Tourner a gauche","Nach links lenken","Sterza a sinistra","Girar a izquierda","Styr vanster"}},
    {PcOption::SteerRight,{"Steer right","Tourner a droite","Nach rechts lenken","Sterza a destra","Girar a derecha","Styr hoger"}},
    {PcOption::Handbrake,{"Handbrake","Frein a main","Handbremse","Freno a mano","Freno de mano","Handbroms"}},
    {PcOption::Camera,{"Change camera","Changer de vue","Kamera wechseln","Cambia visuale","Cambiar camara","Byt kamera"}},
    {PcOption::ShiftUp,{"Shift up","Monter un rapport","Hochschalten","Marcia superiore","Subir marcha","Vaxla upp"}},
    {PcOption::ShiftDown,{"Shift down","Descendre un rapport","Herunterschalten","Marcia inferiore","Bajar marcha","Vaxla ned"}},
    {PcOption::LookBack,{"Look back","Regarder derriere","Nach hinten sehen","Guarda indietro","Mirar atras","Se bakat"}},
    {PcOption::Horn,{"Horn","Klaxon","Hupe","Clacson","Claxon","Tuta"}},
    {PcOption::Nitro,{"Nitro","Nitro","Nitro","Nitro","Nitro","Nitro"}},
    {PcOption::Reset,{"Reset car","Replacer la voiture","Wagen zuruecksetzen","Riposiziona auto","Recolocar coche","Aterstall bilen"}},
    {PcOption::HudToggle,{"Toggle HUD","Afficher l'interface","Instrumente umschalten","Mostra indicatori","Mostrar indicadores","Visa instrument"}}
};

constexpr PcOptionLabel OptionHints[]{
    {PcOption::Audio,{"Adjust the sound and the jukebox.","Regler le son et le jukebox.","Ton und Jukebox einstellen.",
        "Regola audio e jukebox.","Ajusta el sonido y el jukebox.","Justera ljud och jukebox."}},
    {PcOption::Graphics,{"Adjust the display and picture quality.","Regler l'affichage et la qualite d'image.","Anzeige und Bildqualitaet einstellen.",
        "Regola schermo e qualita video.","Ajusta la pantalla y la calidad de imagen.","Justera skarm och bildkvalitet."}},
    {PcOption::AdaptiveTriggers,{"Add resistance to the triggers used for gas and brake.",
        "Ajoute une resistance aux gachettes d'acceleration et de frein.",
        "Trigger-Widerstand beim Gasgeben und Bremsen.",
        "Aggiunge resistenza ai grilletti di acceleratore e freno.",
        "Anade resistencia a los gatillos de acelerador y freno.",
        "Ger motstand i avtryckarna for gas och broms."}},
    {PcOption::PoliceRadioSpeaker,{"Play police radio on the DualSense speaker over USB.",
        "Joue la radio de police sur la DualSense par USB.",
        "Polizeifunk ueber den DualSense-Lautsprecher per USB.",
        "Riproduce la radio della polizia sulla DualSense via USB.",
        "Reproduce la radio policial en el DualSense por USB.",
        "Spelar polisradion i DualSense via USB."}},
    {PcOption::PoliceRadioVolume,{"Adjust the DualSense speaker volume.",
        "Regle le volume du haut-parleur de la DualSense.",
        "Lautstaerke des DualSense-Lautsprechers einstellen.",
        "Regola il volume dell'altoparlante del DualSense.",
        "Ajusta el volumen del altavoz del DualSense.",
        "Staller in volymen pa DualSense-hogtalaren."}},
    {PcOption::Controls,{"Configure the keyboard and gamepad.","Configurer le clavier et la manette.","Tastatur und Gamepad einstellen.",
        "Configura tastiera e gamepad.","Configura el teclado y el mando.","Stall in tangentbord och handkontroll."}},
    {PcOption::Hud,{"Choose what to show during races.","Choisir quoi afficher en course.","Rennanzeigen auswaehlen.",
        "Scegli cosa mostrare in gara.","Elige que mostrar durante la carrera.","Valj vad som visas under lopp."}},
    {PcOption::Profiles,{"Choose, create or delete a profile.","Choisir, creer ou supprimer un profil.","Ein Profil waehlen, erstellen oder loeschen.",
        "Scegli, crea o elimina un profilo.","Elige, crea o elimina un perfil.","Valj, skapa eller radera en profil."}},
    {PcOption::Name,{"Set the names shown for both players.","Regler les noms des deux joueurs.","Die Namen beider Spieler einstellen.",
        "Imposta i nomi dei due giocatori.","Cambia los nombres de ambos jugadores.","Ange namnen pa bada spelarna."}},
    {PcOption::Languages,{"Choose the language of the game.","Choisir la langue du jeu.","Die Sprache des Spiels waehlen.",
        "Scegli la lingua del gioco.","Elige el idioma del juego.","Valj spelets sprak."}},
    {PcOption::Credits,{"See who made the game.","Voir qui a cree le jeu.","Das Entwicklerteam ansehen.",
        "Scopri chi ha creato il gioco.","Descubre quien hizo el juego.","Se vilka som skapade spelet."}},
    {PcOption::ResolutionPage,{"Window mode, resolution and render scale.","Mode d'affichage, resolution et echelle.","Fenstermodus, Aufloesung und Renderskalierung.",
        "Finestra, risoluzione e scala di rendering.","Ventana, resolucion y escala de render.","Fonsterlage, upplosning och renderingsskala."}},
    {PcOption::Window,{"Switch between a window and fullscreen.","Passer de la fenetre au plein ecran.","Zwischen Fenster und Vollbild wechseln.",
        "Passa da finestra a schermo intero.","Cambia entre ventana y pantalla completa.","Vaxla mellan fonster och helskarm."}},
    {PcOption::Resolution,{"Set the display size in pixels.","Regler la taille d'affichage en pixels.","Bildgroesse in Pixeln einstellen.",
        "Imposta la dimensione in pixel.","Ajusta el tamano de pantalla en pixeles.","Stall in bildstorleken i pixlar."}},
    {PcOption::FrameLimit,{"Set the maximum FPS or choose Unlimited.","Limiter les IPS ou choisir Illimite.","Maximale FPS einstellen oder Unbegrenzt waehlen.",
        "Limita gli FPS o scegli Illimitato.","Limita los FPS o elige Sin limite.","Begransa FPS eller valj Obegransat."}},
    {PcOption::Vsync,{"Sync frames to the display to prevent tearing.","Synchroniser les images pour eviter les dechirures.","Bilder synchronisieren, um Tearing zu vermeiden.",
        "Sincronizza i fotogrammi per evitare il tearing.","Sincroniza la imagen para evitar cortes.","Synka bildrutorna for att undvika delad bild."}},
    {PcOption::Distance,{"Set how far away scenery stays visible.","Regler la distance de visibilite du decor.","Sichtweite der Streckenumgebung einstellen.",
        "Regola la distanza visibile dello scenario.","Ajusta hasta donde se ve el escenario.","Stall in hur langt bort omgivningen syns."}},
    {PcOption::Detail,{"Set the model detail of cars and scenery.","Regler le detail des voitures et du decor.","Detailgrad von Autos und Umgebung einstellen.",
        "Regola i dettagli di auto e scenario.","Ajusta el detalle de coches y escenario.","Stall in detaljnivan for bilar och omgivning."}},
    {PcOption::Reflections,{"Set the quality of reflections on cars.","Regler la qualite des reflets des voitures.","Qualitaet der Autoreflexionen einstellen.",
        "Regola la qualita dei riflessi delle auto.","Ajusta la calidad de los reflejos de los coches.","Stall in kvaliteten pa bilarnas reflektioner."}},
    {PcOption::Fps,{"Show the frame rate in the corner.","Afficher les IPS dans un coin de l'ecran.","Bildrate in der Ecke anzeigen.",
        "Mostra gli FPS nell'angolo dello schermo.","Muestra los FPS en la esquina.","Visa bildfrekvensen i hornet."}},
    {PcOption::Advanced,{"Adjust edges, texture filtering and colors.","Regler le lissage, le filtrage et les couleurs.","Kantenglaettung, Filter und Farben einstellen.",
        "Regola bordi, filtraggio e colori.","Ajusta bordes, filtrado y colores.","Justera kanter, filtrering och farger."}},
    {PcOption::Aspect,{"Set the shape of the race picture.","Regler le format d'image en course.","Seitenverhaeltnis im Rennen einstellen.",
        "Regola le proporzioni dell'immagine in gara.","Ajusta la relacion de aspecto en carrera.","Stall in bildformatet under lopp."}},
    {PcOption::RenderScale,{"Set the internal resolution before scaling.","Regler la resolution interne avant mise a l'echelle.","Interne Aufloesung vor der Skalierung einstellen.",
        "Regola la risoluzione interna prima della scala.","Ajusta la resolucion interna antes de escalar.","Stall in upplosningen fore skalning."}},
    {PcOption::Scaling,{"Choose smooth or sharp image scaling.","Choisir une mise a l'echelle douce ou nette.","Weiche oder scharfe Bildskalierung waehlen.",
        "Scegli una scala morbida o nitida.","Elige un escalado suave o nitido.","Valj mjuk eller skarp bildskalning."}},
    {PcOption::AntialiasPage,{"Adjust smoothing on jagged edges.","Regler le lissage des bords.","Treppenkanten glaetten.",
        "Regola il filtro sui bordi seghettati.","Ajusta el suavizado de los bordes dentados.","Stall in utjamning av taggiga kanter."}},
    {PcOption::Antialiasing,{"Choose the method for smoothing edges.","Choisir la methode de lissage des bords.","Methode zur Kantenglaettung waehlen.",
        "Scegli il metodo per levigare i bordi.","Elige el metodo para suavizar bordes.","Valj metod for kantutjamning."}},
    {PcOption::Quality,{"Set the strength of edge smoothing.","Regler le niveau de lissage des bords.","Staerke der Kantenglaettung einstellen.",
        "Regola il livello di antialiasing.","Ajusta el nivel de suavizado de bordes.","Stall in nivan for kantutjamning."}},
    {PcOption::Anisotropy,{"Keep angled textures sharp in the distance.","Garder les textures obliques nettes au loin.","Schraege Texturen in der Ferne schaerfer halten.",
        "Mantieni nitide le texture oblique in lontananza.","Conserva la nitidez de las texturas en angulo.","Hall vinklade texturer skarpa pa avstand."}},
    {PcOption::Brightness,{"Make the whole picture lighter or darker.","Eclaircir ou assombrir toute l'image.","Das ganze Bild heller oder dunkler machen.",
        "Schiarisci o scurisci tutta l'immagine.","Aclara u oscurece toda la imagen.","Gor hela bilden ljusare eller morkare."}},
    {PcOption::Contrast,{"Set the difference between light and dark.","Regler l'ecart entre le clair et le sombre.","Unterschied zwischen Hell und Dunkel einstellen.",
        "Regola la differenza tra chiaro e scuro.","Ajusta la diferencia entre luces y sombras.","Stall in skillnaden mellan ljust och morkt."}},
    {PcOption::Gamma,{"Above 1.00, dark areas become lighter.","Au-dessus de 1.00, les zones sombres s'eclaircissent.","Ueber 1.00 werden dunkle Bereiche heller.",
        "Sopra 1.00, le zone scure si schiariscono.","Por encima de 1.00 se aclaran las zonas oscuras.","Over 1.00 blir morka delar ljusare."}},
    {PcOption::PictureDefaults,{"Restore brightness, contrast and gamma.","Reinitialiser luminosite, contraste et gamma.","Helligkeit, Kontrast und Gamma zuruecksetzen.",
        "Ripristina luminosita, contrasto e gamma.","Restaura brillo, contraste y gamma.","Aterstall ljusstyrka, kontrast och gamma."}},
    {PcOption::Deadzone,{"Set how far the stick moves before steering.","Regler la course du stick avant de tourner.","Stickweg bis zur ersten Lenkreaktion einstellen.",
        "Regola la corsa dello stick prima di sterzare.","Ajusta cuanto se mueve la palanca antes de girar.","Stall in hur langt spaken rors fore styrning."}},
    {PcOption::Steering,{"Set how strongly the car responds to the stick.","Regler la reponse de la voiture au stick.","Lenkempfindlichkeit des Gamepads einstellen.",
        "Regola quanto risponde l'auto allo stick.","Ajusta la sensibilidad de la direccion.","Stall in hur snabbt bilen svarar pa spaken."}},
    {PcOption::IconStyle,{"Choose which controller button icons to show.","Choisir les icones de boutons de la manette.","Tastensymbole fuer das Gamepad auswaehlen.",
        "Scegli le icone dei tasti del gamepad.","Elige los iconos de los botones del mando.","Valj vilka knappikoner som visas."}},
    {PcOption::InputMode,{"Choose which device supplies input.","Choisir le peripherique de commande.","Eingabegeraet auswaehlen.",
        "Scegli il dispositivo di controllo.","Elige el dispositivo de entrada.","Valj vilken enhet som anvands."}},
    {PcOption::Keyboard,{"Change the keys used while driving.","Changer les touches utilisees en course.","Tasten fuer die Fahrt aendern.",
        "Cambia i tasti usati in gara.","Cambia las teclas usadas al conducir.","Andra tangenterna som anvands under lopp."}},
    {PcOption::Gamepad,{"Set up gamepad buttons and stick response.","Regler les boutons et la reponse du stick.","Gamepadtasten und Stickreaktion einstellen.",
        "Regola pulsanti e risposta dello stick.","Configura los botones y la respuesta de la palanca.","Stall in knappar och spakrespons."}},
    {PcOption::Player,{"Set up the second player for split screen.","Configurer le second joueur en ecran partage.","Spieler 2 fuer den geteilten Bildschirm einstellen.",
        "Configura il secondo giocatore a schermo condiviso.","Configura al segundo jugador para pantalla dividida.","Stall in spelare 2 for delad skarm."}},
    {PcOption::Player2Pad,{"Assign a connected gamepad to player 2.","Attribuer une manette connectee au joueur 2.","Spieler 2 ein angeschlossenes Gamepad zuweisen.",
        "Assegna un gamepad collegato al giocatore 2.","Asigna un mando conectado al jugador 2.","Tilldela spelare 2 en ansluten handkontroll."}},
    {PcOption::Layouts,{"Layouts E-H put gas and brake on the triggers.",
        "Les dispositions E-H utilisent les gachettes pour accelerer et freiner.",
        "In Layout E-H liegen Gas und Bremse auf den Triggern.",
        "Nelle configurazioni E-H si accelera e si frena con i grilletti.",
        "Los esquemas E-H usan los gatillos para acelerar y frenar.",
        "Layout E-H anvander avtryckarna for gas och broms."}},
    {PcOption::PadDefaults,{"Restore the default gamepad settings.","Retablir les reglages de manette par defaut.","Standardwerte fuer das Gamepad wiederherstellen.",
        "Ripristina le impostazioni del gamepad.","Restaura los ajustes originales del mando.","Aterstall handkontrollens standardinstallningar."}},
    {PcOption::KeyboardDefaults,{"Restore the default keys for this player.","Retablir les touches par defaut de ce joueur.","Standardtasten dieses Spielers wiederherstellen.",
        "Ripristina i tasti predefiniti del giocatore.","Restaura las teclas de este jugador.","Aterstall spelarens standardtangenter."}},
    {PcOption::Accelerate,{"Bind the key that applies the throttle.","Attribuer la touche d'acceleration.","Taste fuer das Gaspedal zuweisen.",
        "Assegna il tasto dell'acceleratore.","Asigna la tecla del acelerador.","Tilldela tangenten for gas."}},
    {PcOption::Brake,{"Bind the key that slows the car.","Attribuer la touche de freinage.","Taste zum Bremsen zuweisen.",
        "Assegna il tasto del freno.","Asigna la tecla del freno.","Tilldela tangenten for bromsen."}},
    {PcOption::SteerLeft,{"Bind the key that steers the car left.","Attribuer la touche pour braquer a gauche.","Taste zum Linkslenken zuweisen.",
        "Assegna il tasto per sterzare a sinistra.","Asigna la tecla para girar a la izquierda.","Tilldela tangenten for vansterstyrning."}},
    {PcOption::SteerRight,{"Bind the key that steers the car right.","Attribuer la touche pour braquer a droite.","Taste zum Rechtslenken zuweisen.",
        "Assegna il tasto per sterzare a destra.","Asigna la tecla para girar a la derecha.","Tilldela tangenten for hoger styrning."}},
    {PcOption::Handbrake,{"Bind the key that locks the rear wheels.","Attribuer la touche qui bloque les roues arriere.","Taste zum Blockieren der Hinterraeder zuweisen.",
        "Assegna il tasto che blocca le ruote posteriori.","Asigna la tecla que bloquea las ruedas traseras.","Tilldela tangenten som laser bakhjulen."}},
    {PcOption::Nitro,{"Bind the boost key for cars with nitro.","Attribuer la touche de boost des voitures a nitro.","Taste fuer den Nitro-Schub zuweisen.",
        "Assegna il tasto del turbo a nitro.","Asigna la tecla de impulso de los coches con nitro.","Tilldela tangenten for nitroboost."}},
    {PcOption::LookBack,{"Bind the key that looks behind the car.","Attribuer la touche pour regarder derriere l'auto.","Taste fuer den Blick nach hinten zuweisen.",
        "Assegna il tasto per guardare dietro l'auto.","Asigna la tecla para mirar detras del coche.","Tilldela tangenten for att se bakom bilen."}},
    {PcOption::Camera,{"Bind the key that cycles driving views.","Attribuer la touche qui change la vue de conduite.","Taste zum Wechseln der Fahransicht zuweisen.",
        "Assegna il tasto per cambiare inquadratura.","Asigna la tecla para cambiar la vista de conduccion.","Tilldela tangenten som byter kameravy."}},
    {PcOption::Horn,{"Bind the key that sounds the car horn.","Attribuer la touche du klaxon.","Taste fuer die Hupe zuweisen.",
        "Assegna il tasto del clacson.","Asigna la tecla del claxon.","Tilldela tangenten for tutan."}},
    {PcOption::ShiftDown,{"Bind the key for a lower manual gear.","Attribuer la touche pour retrograder en mode manuel.","Taste zum manuellen Herunterschalten zuweisen.",
        "Assegna il tasto per scalare marcia.","Asigna la tecla para reducir la marcha manual.","Tilldela tangenten for manuell nedvaxling."}},
    {PcOption::ShiftUp,{"Bind the key for a higher manual gear.","Attribuer la touche pour monter un rapport en manuel.","Taste zum manuellen Hochschalten zuweisen.",
        "Assegna il tasto per salire di marcia.","Asigna la tecla para subir la marcha manual.","Tilldela tangenten for manuell uppvaxling."}},
    {PcOption::HudToggle,{"Bind the key that changes the race HUD view.","Attribuer la touche qui change l'affichage en course.","Taste zum Wechseln der Rennanzeige zuweisen.",
        "Assegna il tasto per cambiare l'HUD di gara.","Asigna la tecla para cambiar el HUD de carrera.","Tilldela tangenten som byter loppets HUD."}},
    {PcOption::Reset,{"Bind the key that puts the car back on the road.","Attribuer la touche qui remet la voiture sur la route.","Taste zum Zuruecksetzen auf die Strasse zuweisen.",
        "Assegna il tasto per rimettere l'auto in pista.","Asigna la tecla para devolver el coche a la pista.","Tilldela tangenten som staller bilen tillbaka pa vagen."}},
    {PcOption::ZoneFreeze,{"Bind the key for the freeze-frame camera.","Attribuer la touche de la camera avec arret sur image.","Taste fuer die Standbildkamera zuweisen.",
        "Assegna il tasto della telecamera a fermo immagine.","Asigna la tecla de la camara con imagen congelada.","Tilldela tangenten for kamerans frysbild."}},
    {PcOption::ZoneZoom,{"Bind the key for the Zone zoom camera.","Attribuer la touche du zoom de la camera Zone.","Taste fuer den Zoom der Zonenkamera zuweisen.",
        "Assegna il tasto dello zoom della telecamera Zone.","Asigna la tecla del zoom de la camara Zone.","Tilldela tangenten for Zone-kamerans zoom."}},
    {PcOption::Profile,{"Load this profile and its saved progress.","Charger ce profil et sa progression.","Dieses Profil und seinen Fortschritt laden.",
        "Carica questo profilo e i suoi progressi.","Carga este perfil y su progreso.","Las in profilen och dess sparade framsteg."}},
    {PcOption::NewProfile,{"Create a profile with separate progress.","Creer un profil avec sa propre progression.","Ein Profil mit eigenem Spielfortschritt erstellen.",
        "Crea un profilo con progressi separati.","Crea un perfil con progreso independiente.","Skapa en profil med egna sparade framsteg."}},
    {PcOption::English,{"Switch the game to English.","Passer le jeu en anglais.","Das Spiel auf Englisch umstellen.",
        "Passa all'inglese.","Cambia el juego a ingles.","Byt spelets sprak till engelska."}},
    {PcOption::French,{"Switch the game to French.","Passer le jeu en francais.","Das Spiel auf Franzoesisch umstellen.",
        "Passa al francese.","Cambia el juego a frances.","Byt spelets sprak till franska."}},
    {PcOption::German,{"Switch the game to German.","Passer le jeu en allemand.","Das Spiel auf Deutsch umstellen.",
        "Passa al tedesco.","Cambia el juego a aleman.","Byt spelets sprak till tyska."}},
    {PcOption::Italian,{"Switch the game to Italian.","Passer le jeu en italien.","Das Spiel auf Italienisch umstellen.",
        "Passa all'italiano.","Cambia el juego a italiano.","Byt spelets sprak till italienska."}},
    {PcOption::Spanish,{"Switch the game to Spanish.","Passer le jeu en espagnol.","Das Spiel auf Spanisch umstellen.",
        "Passa allo spagnolo.","Cambia el juego a espanol.","Byt spelets sprak till spanska."}},
    {PcOption::Swedish,{"Switch the game to Swedish.","Passer le jeu en suedois.","Das Spiel auf Schwedisch umstellen.",
        "Passa allo svedese.","Cambia el juego a sueco.","Byt spelets sprak till svenska."}},
};
enum class PcPage : std::size_t {Graphics,Resolution,Advanced,Antialiasing,Controls,Keyboard,Gamepad,PlayerTwo,KeyboardTwo,Profiles,Languages,Options};
struct PcPageDescription {
    const char* name;
    Translation title;
    std::initializer_list<PcOption> options;
};
using Row=PcOption;
constexpr std::size_t VisiblePcRows=7;
const std::array<PcPageDescription,12> PcPages{{
    {"PC Graphics",{"Graphics","Graphismes","Grafik","Grafica","Graficos","Grafik"},
        {Row::ResolutionPage,Row::FrameLimit,Row::Vsync,Row::Distance,Row::Detail,Row::Reflections,Row::Fps,Row::Advanced}},
    {"PC Resolution",{"Resolution","Resolution","Aufloesung","Risoluzione","Resolucion","Upplosning"},
        {Row::Window,Row::Resolution,Row::Aspect,Row::RenderScale,Row::Scaling}},
    {"PC Advanced",{"Advanced","Avance","Erweitert","Avanzate","Avanzado","Avancerat"},
        {Row::AntialiasPage,Row::Anisotropy,Row::Brightness,Row::Contrast,Row::Gamma,Row::PictureDefaults}},
    {"PC Antialiasing",{"Antialiasing","Anticrenelage","Kantenglaettung","Antialiasing","Suavizado","Kantutjamning"},
        {Row::Antialiasing,Row::Quality}},
    {"PC Controls",{"Controls","Commandes","Steuerung","Comandi","Controles","Kontroller"},
        {Row::Keyboard,Row::Gamepad,Row::Player,Row::InputMode}},
    {"PC Keyboard",{"Keyboard","Clavier","Tastatur","Tastiera","Teclado","Tangentbord"},
        {Row::Accelerate,Row::Brake,Row::SteerLeft,Row::SteerRight,Row::Handbrake,Row::Nitro,Row::LookBack,
         Row::Camera,Row::Horn,Row::ShiftDown,Row::ShiftUp,Row::HudToggle,Row::Reset,Row::ZoneFreeze,Row::ZoneZoom,Row::KeyboardDefaults}},
    {"PC Gamepad",{"Gamepad","Manette","Gamepad","Gamepad","Mando","Handkontroll"},
        {Row::Layouts,Row::Deadzone,Row::Steering,Row::IconStyle,Row::PadDefaults,Row::AdaptiveTriggers,Row::PoliceRadioSpeaker,Row::PoliceRadioVolume}},
    {"PC Player 2",{"Player 2","Joueur 2","Spieler 2","Giocatore 2","Jugador 2","Spelare 2"},
        {Row::InputMode,Row::Keyboard,Row::Player2Pad,Row::AdaptiveTriggers,Row::PoliceRadioSpeaker,Row::PoliceRadioVolume}},
    {"PC Keyboard P2",{"Player 2 Keyboard","Clavier joueur 2","Tastatur Spieler 2","Tastiera giocatore 2","Teclado jugador 2","Spelare 2 tangentbord"},
        {Row::Accelerate,Row::Brake,Row::SteerLeft,Row::SteerRight,Row::Handbrake,Row::Nitro,Row::LookBack,
         Row::Camera,Row::Horn,Row::ShiftDown,Row::ShiftUp,Row::HudToggle,Row::Reset,Row::ZoneFreeze,Row::ZoneZoom,Row::KeyboardDefaults}},
    {"PC Profiles",{"Profiles","Profils","Profile","Profili","Perfiles","Profiler"},
        {Row::Profile,Row::Profile,Row::Profile,Row::Profile,Row::Profile,Row::Profile,Row::NewProfile}},
    {"PC Languages",{"Languages","Langues","Sprachen","Lingue","Idiomas","Sprak"},
        {Row::English,Row::French,Row::German,Row::Italian,Row::Spanish,Row::Swedish}},
    {"Options",{"Options","Options","Optionen","Opzioni","Opciones","Alternativ"},
        {Row::Audio,Row::Controls,Row::Graphics,Row::Hud,Row::Profiles,Row::Name,Row::Languages,Row::Credits}}
}};

const PcPageDescription& Describe(PcPage page) {return PcPages[static_cast<std::size_t>(page)];}
constexpr float PcDrawDistance[]{1.0f,1.5f,2.0f,3.0f,4.0f};
constexpr int PcAnisotropy[]{0,2,4,8,16};
int SliderSteps(Row option)
{
    switch(option) {
    case Row::Distance:return static_cast<int>(std::size(PcDrawDistance))-1;
    case Row::Brightness:case Row::Contrast:return 20;
    case Row::Gamma:return 30;
    case Row::Deadzone:return 25;
    case Row::Steering:return 15;
    case Row::PoliceRadioVolume:return 100;
    default:return 0;
    }
}
std::size_t MenuLanguage(const MainMenuScreenServices& services)
{
    return static_cast<std::size_t>(std::clamp(services.ui->language->currentLanguage,0,5));
}

void OpenPcPage(MainMenuScreenServices& services,PcPage page)
{
    services.screens->ChangeToScreen(Describe(page).name,true);
}

void ClonePcPages(MainMenuScreenServices& services)
{
    for(std::size_t i=0;i<PcPages.size();++i) {
        const auto& page=PcPages[i];
        const auto key=i==static_cast<std::size_t>(PcPage::Options)?0x95ad096bu:hashString(page.name);
        const auto rows=std::min(page.options.size(),VisiblePcRows);
        services.ui->resources->CloneOptionsPage(key,rows);
        if(std::any_of(page.options.begin(),page.options.end(),[](Row row){return SliderSteps(row)>0;}))
            services.ui->resources->AddNativeOptionSliders(key,rows);
    }
    services.ui->resources->AddNativeAudioMaster(0xe116761cu);
    auto& resources=*services.ui->resources;
    resources.AddNativeBackHints();
    const auto profileKey=hashString(Describe(PcPage::Profiles).name);
    for(const auto& hint: {std::pair{"PC_BACK_ICON","PC_DEL_ICON"},std::pair{"PC_BACK_TEXT","PC_DEL_TEXT"}}) {
        if(const auto* source=static_cast<const FlailerTextChunk*>(resources.FindObject(profileKey,hashString(hint.first),1))) {
            auto& copy=resources.CloneNativeText(profileKey,*source,hashString(hint.second));
            copy.SetTopLeftX(source->topLeftX+96.0f);copy.visible=0;
        }
    }
}


int BindingIndex(PcOption option)
{
    switch(option) {
    case Row::Accelerate:return 14;
    case Row::Brake:return 15;
    case Row::SteerLeft:return 16;
    case Row::SteerRight:return 17;
    case Row::Handbrake:return 11;
    case Row::Camera:return 12;
    case Row::ShiftUp:return 9;
    case Row::ShiftDown:return 8;
    case Row::LookBack:return 10;
    case Row::Horn:return 13;
    case Row::Nitro:return 2;
    case Row::Reset:return 0;
    case Row::HudToggle:return 4;
    case Row::ZoneFreeze:return 20;
    case Row::ZoneZoom:return 21;
    default:return -1;
    }
}

template<typename T,std::size_t n>
T Cycle(T value,const T (&choices)[n],int direction)
{
    auto found=std::find(std::begin(choices),std::end(choices),value);
    const auto index=found==std::end(choices)?(direction>0?0:n-1):
        static_cast<std::size_t>((found-std::begin(choices)+static_cast<std::ptrdiff_t>(n)+direction)%n);
    return choices[index];
}

std::pair<int,int> CycleDisplayChoice(std::pair<int,int> value,const std::vector<std::pair<int,int>>& choices,int direction)
{
    if(choices.empty())return value;
    const auto found=std::find(choices.begin(),choices.end(),value);
    const auto count=static_cast<std::ptrdiff_t>(choices.size());
    const auto index=found==choices.end()?(direction>0?0:count-1):(found-choices.begin()+count+direction)%count;
    return choices[static_cast<std::size_t>(index)];
}
class NativeOptionsScreen final:public OptionsScreenBase {
public:
    NativeOptionsScreen(MainMenuScreenServices& services,std::uint32_t screen,PcPage page)
        :OptionsScreenBase(services,screen),page_(page),description_(Describe(page)),host_(*services.pcHost),
         blurb_(*services.ui,screen,0x75980cf0u,0x75980cf0u,0x70a404ecu),
         arrows_(*services.ui,screen,0xd1cda594u,0x9345e4a7u)
    {
        auto& resources=*services.ui->resources;
        resources.SetLocalizedText(screen,hashString("PC_OPTIONS_TITLE"),description_.title[MenuLanguage(services)]);
        for(std::size_t row=0;row<std::min(description_.options.size(),VisiblePcRows);++row) {
            if(const auto* object=static_cast<const FlailerTextChunk*>(resources.FindObject(screen,ValueKey(row),1)))
                valueScale_[row]=object->nativeFitBaseX?object->nativeFitBaseX:object->scaleX;
            if(const auto* object=static_cast<const FlailerTextChunk*>(
                resources.FindObject(screen,0x52e039f3u+static_cast<std::uint32_t>(row),1)))
                labelScale_[row]=object->nativeFitBaseX?object->nativeFitBaseX:object->scaleX;
            if(resources.FindObject(screen,SliderBarKey(row),2)) {
                sliders_[row].emplace(*services.ui,screen,0x52e039f3u+static_cast<std::uint32_t>(row),
                    SliderBarKey(row),SliderThumbKey(row),0.0f);
            }
        }
        if(page_==PcPage::Profiles)RefreshProfiles();
        else Redraw();
        for(auto& slider:sliders_)if(slider)slider->ShowValue(slider->position.desiredValue,true);
    }
    ~NativeOptionsScreen() override
    {
        EndRenderPreview();
        if(capture_>=0)host_.BeginBindingCapture(-1);
    }
    static void Callback(ScreenMessage message,ScreenCallbackData* data)
    {
        if(message==ScreenMessage::ScreenActivate) {
            Require(data,"PC page activation requires screen identity");
            auto& services=BoundServices();
            const auto page=std::find_if(PcPages.begin(),PcPages.end(),[&](const auto& item) {
                return hashString(item.name)==data->key;
            });
            Require(page!=PcPages.end() && services.pcHost,"Unregistered PC options page");
            Install(std::make_unique<NativeOptionsScreen>(services,data->key,static_cast<PcPage>(page-PcPages.begin())));
        } else BaseCallback(message,data);
    }
private:
    static std::uint32_t ValueKey(std::size_t row) {return hashString("PC_OPTION_VALUE")+static_cast<std::uint32_t>(row);}
    static std::uint32_t SliderBarKey(std::size_t row) {return hashString("PC_OPTION_SLIDER_BAR")+static_cast<std::uint32_t>(row);}
    static std::uint32_t SliderThumbKey(std::size_t row) {return hashString("PC_OPTION_SLIDER_THUMB")+static_cast<std::uint32_t>(row);}
    std::size_t Language() const {return MenuLanguage(state_->services);}
    bool OptionVisible(Row option) const
    {
        return hasDualSense_ || (option!=Row::AdaptiveTriggers && option!=Row::PoliceRadioSpeaker &&
            option!=Row::PoliceRadioVolume);
    }
    std::size_t OptionCount() const
    {
        if(page_==PcPage::Profiles)return profileNames_.size()+1;
        return static_cast<std::size_t>(std::count_if(description_.options.begin(),description_.options.end(),
            [&](Row option){return OptionVisible(option);}));
    }
    std::size_t RowCount() const {return std::min(OptionCount(),VisiblePcRows);}
    std::int32_t Port() const {return page_==PcPage::KeyboardTwo || page_==PcPage::PlayerTwo?1:0;}
    PcOption OptionAt(std::size_t row) const
    {
        if(page_!=PcPage::Profiles) {
            for(const auto option:description_.options)if(OptionVisible(option) && row--==0)return option;
            throw std::out_of_range("Native options row");
        }
        return row<profileNames_.size()?Row::Profile:Row::NewProfile;
    }
    int SliderIndex(Row option,const NativePcSettings& settings) const
    {
        int index{};
        switch(option) {
        case Row::Distance:
            index=static_cast<int>(std::min_element(std::begin(PcDrawDistance),std::end(PcDrawDistance),
                [&](float a,float b){return std::abs(a-settings.render.drawDistanceScale)<
                    std::abs(b-settings.render.drawDistanceScale);})-std::begin(PcDrawDistance));break;
        case Row::Brightness:index=(settings.render.brightness+52)/5;break;
        case Row::Contrast:index=(settings.render.contrast-48)/5;break;
        case Row::Gamma:index=(settings.render.gamma-48)/5;break;
        case Row::Deadzone: {
            const auto zone=settings.input.profiles[Port()].deadzone[2];
            index=((zone>=0?zone:settings.input.leftDeadzone)*50+16383)/32767;break;
        }
        case Row::Steering:index=(settings.input.steeringSensitivity-45)/10;break;
        case Row::PoliceRadioVolume:index=settings.input.profiles[Port()].policeRadioVolume;break;
        default:break;
        }
        return std::clamp(index,0,SliderSteps(option));
    }
    bool SetSliderIndex(Row option,int index,NativePcSettings& settings) const
    {
        const auto assign=[](auto& setting,auto value) {
            if(setting==value)return false;
            setting=value;
            return true;
        };
        switch(option) {
        case Row::Distance:return assign(settings.render.drawDistanceScale,PcDrawDistance[index]);
        case Row::Brightness:return assign(settings.render.brightness,-50+index*5);
        case Row::Contrast:return assign(settings.render.contrast,50+index*5);
        case Row::Gamma:return assign(settings.render.gamma,50+index*5);
        case Row::Deadzone:return assign(settings.input.profiles[Port()].deadzone[2],(index*2*32767+50)/100);
        case Row::Steering:return assign(settings.input.steeringSensitivity,50+index*10);
        case Row::PoliceRadioVolume:return assign(settings.input.profiles[Port()].policeRadioVolume,index);
        default:return false;
        }
    }
    void EndRenderPreview() noexcept
    {
        if(std::exchange(renderPreview_,false))configureNativeRenderSettings(host_.PcSettings()->render);
        if(std::exchange(radioPreview_,false))host_.PreviewDualSenseVolume(Port(),std::nullopt);
    }
    void AdjustSlider(std::size_t row,Row option,ScreenMessage message,ScreenCallbackData& data)
    {
        const bool drag=message==ScreenMessage::SliderDrag;
        const bool pointer=drag || message==ScreenMessage::SliderCommit;
        auto settings=*host_.PcSettings();
        const int steps=SliderSteps(option),current=SliderIndex(option,settings);
        const int index=pointer ? (std::clamp(data.value,0,10000)*steps+5000)/10000 :
            std::clamp(current+(message==ScreenMessage::Left?-1:1),0,steps);
        auto& slider=*sliders_[row];
        const float fraction=static_cast<float>(index)/steps;
        const bool changed=slider.position.desiredValue!=fraction;
        data.sound=0;
        const bool changedSetting=(pointer || index!=current) && SetSliderIndex(option,index,settings);
        if(drag) {
            const bool graphicsSlider=option==Row::Distance || option==Row::Brightness || option==Row::Contrast || option==Row::Gamma;
            if(graphicsSlider && (!renderPreview_ || changed)) {
                configureNativeRenderSettings(settings.render);
                renderPreview_=true;
            }
            if(option==Row::PoliceRadioVolume) {
                host_.PreviewDualSenseVolume(Port(),index);
                radioPreview_=true;
            }
            slider.ShowValue(fraction,true);
            SetFittedText(ValueKey(row),Value(option,settings),valueScale_[row],96.0f);
            if(changed)slider.PlayChange();
            return;
        }
        EndRenderPreview();
        if(!changedSetting) {Redraw();return;}
        if(Save(settings,false) && changed)slider.PlayChange();
    }
    static const char* OptionLabel(PcOption option,std::size_t lang)
    {
        for(const auto& label:Labels)if(label.option==option)return label.text[lang];
        return "";
    }
    std::string Value(PcOption option,const NativePcSettings& settings) const
    {
        constexpr Translation on{"On","Oui","An","Si","Si","Pa"};
        constexpr Translation off{"Off","Non","Aus","No","No","Av"};
        constexpr Translation windowed{"Windowed","Fenetre","Fenster","Finestra","Ventana","Fonster"};
        constexpr Translation fullscreen{"Fullscreen","Plein ecran","Vollbild","Schermo intero","Pantalla completa","Helskarm"};
        constexpr Translation uncapped{"Unlimited","Illimite","Unbegrenzt","Illimitato","Sin limite","Obegransat"};
        const auto lang=Language();
        const auto binding=BindingIndex(option);
        if(binding>=20)return platform::NativeKeyName(settings.input.zoneKeys[Port()][binding-20]);
        if(binding>=0)return platform::NativeKeyName(platform::NativeKeyboardBindings(settings.input,Port())[binding]);
        if(option>=Row::English && option<=Row::Swedish) {
            constexpr Translation selected{"Selected","Selectionne","Ausgewaehlt","Selezionato","Seleccionado","Valt"};
            return static_cast<int>(option)-static_cast<int>(Row::English)==static_cast<int>(lang)?selected[lang]:"";
        }
        switch(option) {
        case Row::Resolution: {
            if(settings.fullscreen && !settings.fullscreenSize.first) {
                constexpr Translation desktop{"Desktop","Bureau","Desktop","Desktop","Escritorio","Skrivbord"};
                return desktop[lang];
            }
            const auto size=settings.fullscreen?settings.fullscreenSize:std::make_pair(settings.width,settings.height);
            return std::to_string(size.first)+" x "+std::to_string(size.second);
        }
        case Row::Window:return (settings.fullscreen?fullscreen:windowed)[lang];
        case Row::Vsync:return (settings.vsync?on:off)[lang];
        case Row::FrameLimit:return settings.frameLimit?std::to_string(settings.frameLimit)+" FPS":uncapped[lang];
        case Row::Distance: {
            char value[24]{};
            std::snprintf(value,sizeof(value),"%.0f%%",settings.render.drawDistanceScale*100.0f);
            return value;
        }
        case Row::RenderScale: {
            constexpr Translation native{"Native","Natif","Nativ","Nativo","Nativo","Inbyggd"};
            return settings.renderScale?std::to_string(settings.renderScale)+"x":native[lang];
        }
        case Row::Aspect: {
            constexpr Translation automatic{"Automatic","Automatique","Automatisch","Automatico","Automatico","Automatisk"};
            if(!settings.raceAspect.first)return automatic[lang];
            return std::to_string(settings.raceAspect.first)+":"+std::to_string(settings.raceAspect.second);
        }
        case Row::Scaling: {
            constexpr Translation sharp{"Sharp","Net","Scharf","Nitido","Nitido","Skarp"};
            constexpr Translation smooth{"Smooth","Lisse","Weich","Morbido","Suave","Mjuk"};
            return (settings.sharpScaling?sharp:smooth)[lang];
        }
        case Row::Detail: {
            constexpr Translation maximum{"Maximum","Maximum","Maximal","Massimo","Maximo","Maximal"};
            constexpr Translation original{"Original","Original","Original","Originale","Original","Original"};
            return (settings.render.maximumDetail?maximum:original)[lang];
        }
        case Row::Reflections: {
            constexpr Translation high{"High","Eleves","Hoch","Alti","Altos","Hoga"};
            if(settings.render.reflections==2)return high[lang];
            return settings.render.reflections==1?OptionLabel(Row::Advanced,lang):"PS2";
        }
        case Row::Antialiasing:
            if(settings.antialiasing==NativeAntialiasing::Off)return off[lang];
            if(settings.antialiasing==NativeAntialiasing::Fxaa)return "FXAA";
            return settings.antialiasing>=NativeAntialiasing::SmaaHigh?"SMAA":"MSAA";
        case Row::Quality:
            if(settings.antialiasing==NativeAntialiasing::SmaaHigh)return "2x";
            if(settings.antialiasing==NativeAntialiasing::SmaaUltra)return "4x";
            if(settings.antialiasing<NativeAntialiasing::Msaa2)return "-";
            return std::to_string(1<<(static_cast<int>(settings.antialiasing)-1))+"x";
        case Row::Anisotropy:return settings.textureFilter==NativeTextureFilter::Anisotropic?
            std::to_string(settings.anisotropy)+"x":off[lang];
        case Row::Brightness:return (settings.render.brightness>0?"+":"")+std::to_string(settings.render.brightness);
        case Row::Contrast:return std::to_string(settings.render.contrast)+"%";
        case Row::Gamma: {
            char text[16]{};
            std::snprintf(text,sizeof(text),"%d.%02d",settings.render.gamma/100,settings.render.gamma%100);
            return text;
        }
        case Row::Fps:return (settings.showFps?on:off)[lang];
        case Row::AdaptiveTriggers:return (settings.input.profiles[Port()].adaptiveTriggers?on:off)[lang];
        case Row::PoliceRadioSpeaker:return (settings.input.profiles[Port()].policeRadioSpeaker?on:off)[lang];

        case Row::Deadzone: {
            const auto zone=settings.input.profiles[Port()].deadzone[2];
            return std::to_string(((zone>=0?zone:settings.input.leftDeadzone)*100+16383)/32767)+"%";
        }
        case Row::Steering:return std::to_string(settings.input.steeringSensitivity)+"%";
        case Row::PoliceRadioVolume:return std::to_string(settings.input.profiles[Port()].policeRadioVolume)+"%";
        case Row::IconStyle: {
            const char* styles[]{"Auto","Xbox","PlayStation"};
            return styles[settings.input.promptStyle];
        }
        case Row::InputMode: {
            constexpr Translation modes[]={
                {"Automatic","Automatique","Automatisch","Automatico","Automatico","Automatiskt"},
                {"Keyboard","Clavier","Tastatur","Tastiera","Teclado","Tangentbord"},
                {"Gamepad","Manette","Gamepad","Gamepad","Mando","Handkontroll"},
                {"Keyboard and gamepad","Clavier et manette","Tastatur und Gamepad","Tastiera e gamepad","Teclado y mando","Tangentbord och handkontroll"},
                {"Disabled","Desactive","Deaktiviert","Disattivato","Desactivado","Av"}};
            return modes[static_cast<std::size_t>(settings.input.deviceMode[Port()])][lang];
        }
        case Row::Player2Pad: {
            const auto device=host_.InputDeviceName(1);
            if(!device.empty())return device;
            constexpr Translation disconnected{"Not connected","Non connectee","Nicht verbunden","Non collegato","Sin conectar","Inte ansluten"};
            return disconnected[lang];
        }
        default:return {};
        }
    }
    void SetFittedText(std::uint32_t key,const std::string& text,float scale,float width)
    {
        auto& resources=*state_->services.ui->resources;
        auto* object=static_cast<FlailerTextChunk*>(resources.FindObject(state_->screen,key,1));
        if(!object)return;
        resources.SetLocalizedText(state_->screen,key,text.c_str());
        if(!object->nativeFitBaseX) {
            object->nativeFitBaseX=scale;
            object->nativeFitBaseY=object->scaleY;
        }
        object->SetScaleX(scale);
        const auto measured=resources.ScaledTextWidth(*object,state_->services.ui->language);
        const auto fit=measured>width?width/measured:1.0f;
        object->SetScaleX(scale*fit);
        object->SetScaleY(object->nativeFitBaseY*fit);
    }
    void Redraw()
    {
        EndRenderPreview();
        hasDualSense_=host_.HasDualSense(Port());
        offset_=std::min(offset_,OptionCount()-RowCount());
        const auto* settings=host_.PcSettings();
        auto& services=state_->services;
        if(services.screens->sliderDragScreen==services.screens->active) {
            services.screens->sliderDragScreen=nullptr;services.screens->sliderDragBar=0;
        }
        auto& resources=*services.ui->resources;
        constexpr Translation back{"Back","Retour","Zurueck","Indietro","Atras","Tillbaka"};
        resources.SetLocalizedText(state_->screen,hashString("PC_OPTIONS_TITLE"),description_.title[Language()]);
        constexpr Translation exit{"Exit","Quitter","Beenden","Esci","Salir","Avsluta"};
        const bool choosing=page_==PcPage::Profiles && services.profiles->ChoosingAtStartup();
        resources.SetLocalizedText(state_->screen,hashString("PC_BACK_TEXT"),(choosing?exit:back)[Language()]);
        resources.SetObjectVisibility(state_->screen,0xedf2e4eeu,offset_>0);
        resources.SetObjectVisibility(state_->screen,0x20d73d01u,offset_+RowCount()<OptionCount());
        if(page_==PcPage::Gamepad || page_==PcPage::PlayerTwo)UpdateGamepadRows();
        for(std::size_t row=0;row<RowCount();++row) {
            const auto option=OptionAt(offset_+row);
            auto value=Value(option,*settings);
            if(static_cast<int>(offset_+row)==capture_)value="...";
            auto label=std::string(OptionLabel(option,Language()));
            if(option==Row::Profile) {
                constexpr Translation current{"Current","Actuel","Aktiv","Attivo","Actual","Aktiv"};
                const auto& profile=profileNames_[offset_+row];
                label=services.profiles->ProfileName(profile);
                if(profile==services.profiles->ActiveProfile())value=current[Language()];
            }
            const auto key=0x52e039f3u+static_cast<std::uint32_t>(row);
            const auto steps=SliderSteps(option);
            if(sliders_[row]) {
                resources.SetObjectVisibility(state_->screen,SliderBarKey(row),steps!=0);
                resources.SetObjectVisibility(state_->screen,SliderThumbKey(row),steps!=0);
                if(steps)sliders_[row]->ShowValue(static_cast<float>(SliderIndex(option,*settings))/steps);
            }
            if(auto* valueObject=resources.FindObject(state_->screen,ValueKey(row),1))
                if(const auto* labelObject=resources.FindObject(state_->screen,key,1))
                    valueObject->SetTopLeftY(labelObject->topLeftY+(steps?-4.0f:6.0f));
            resources.SetObjectVisibility(state_->screen,key,1);
            SetFittedText(key,label,labelScale_[row],
                value.empty()?405.0f:233.0f);
            resources.SetObjectVisibility(state_->screen,ValueKey(row),!value.empty());
            SetFittedText(ValueKey(row),value,valueScale_[row],steps?96.0f:168.0f);
        }
    }
    void UpdateGamepadRows()
    {
        auto& services=state_->services;
        auto& resources=*services.ui->resources;
        auto* panel=services.screens->active?services.screens->active->FindFlailerScreen(state_->screen):nullptr;
        for(std::size_t row=0;row<std::min(description_.options.size(),VisiblePcRows);++row) {
            const auto key=0x52e039f3u+static_cast<std::uint32_t>(row);
            const bool visible=row<RowCount();
            if(panel && panel->menu)if(auto* item=panel->menu->FindItem(key)) {
                if(visible)item->SetMenuItemState(1);else item->ClearMenuItemState(1);
            }
            if(visible)continue;
            resources.SetObjectVisibility(state_->screen,key,0);
            resources.SetObjectVisibility(state_->screen,ValueKey(row),0);
            if(sliders_[row]) {
                resources.SetObjectVisibility(state_->screen,SliderBarKey(row),0);
                resources.SetObjectVisibility(state_->screen,SliderThumbKey(row),0);
            }
        }
        if(panel && panel->menu && highlightedRow_>=RowCount()) {
            highlightedRow_=RowCount()-1;
            panel->menu->SetActiveMenuItem(0x52e039f3u+static_cast<std::uint32_t>(highlightedRow_));
        }
    }
    void RefreshProfiles()
    {
        auto& services=state_->services;
        profileNames_=services.profiles->Profiles();
        offset_=std::min(offset_,OptionCount()-RowCount());
        Redraw();
        auto* source=services.screens->active->FindFlailerScreen(static_cast<std::int32_t>(state_->screen));
        if(!source || !source->menu)return;
        auto& resources=*services.ui->resources;
        for(std::size_t row=0;row<description_.options.size();++row) {
            const auto key=0x52e039f3u+static_cast<std::uint32_t>(row);
            auto* item=source->menu->FindItem(key);
            if(!item)continue;
            if(row<RowCount())item->SetMenuItemState(1);
            else {
                item->ClearMenuItemState(1);
                resources.SetObjectVisibility(state_->screen,key,0);
                resources.SetObjectVisibility(state_->screen,ValueKey(row),0);
            }
        }
    }
    void AskDelete(const std::string& profile,std::int32_t port)
    {
        auto& services=state_->services;
        if(profile==services.profiles->ActiveProfile())return;
        constexpr Translation prompt{"Delete this profile?","Supprimer ce profil?","Dieses Profil loeschen?",
            "Eliminare il profilo?","Eliminar este perfil?","Ta bort profilen?"};
        FlailerPopupConfig popup;
        popup.sourceNode=services.screens->active;popup.screenKey=0x037f3133u;
        popup.flags=3;popup.buttonCount=2;popup.message=prompt[Language()];
        popup.buttonLanguageIds={{0x38277,0x38278,0}};popup.buttonResults={{1,0,0}};
        popup.initialButton=1;popup.joystickPort=port;
        if(services.screens->SummonPopup(popup)>=0)deleteProfile_=profile;
    }
    bool Save(const NativePcSettings& settings,bool display)
    {
        std::string error;
        const auto previous=*host_.PcSettings();
        if(host_.SavePcSettings(settings,error)) {
            const bool controls=page_==PcPage::Keyboard || page_==PcPage::KeyboardTwo ||
                page_==PcPage::PlayerTwo || page_==PcPage::Gamepad;
            if(controls && !state_->services.profiles->SaveProfileControls(settings)) {
                error=state_->services.profiles->LastError();
                std::string rollback;
                if(!host_.SavePcSettings(previous,rollback))error+=". "+rollback;
                ReportError(error);
                Redraw();
                return false;
            }
            if(display)state_->services.applyPcDisplay(settings);
        } else {ReportError(error);Redraw();return false;}
        Redraw();
        return true;
    }
    void ReportError(const std::string& error)
    {
        host_.RecordSessionEvent("menu.settings_failed",error.c_str());
        constexpr Translation messages{"Settings could not be saved.","Impossible d'enregistrer.",
            "Speichern fehlgeschlagen.","Salvataggio non riuscito.","No se pudo guardar.","Det gick inte att spara."};
        blurb_.NewText(messages[Language()]);
    }
    void ShowHint(PcOption option)
    {
        const bool editable=BindingIndex(option)<0 && !Value(option,*host_.PcSettings()).empty() &&
            option!=Row::Profile && page_!=PcPage::Languages;
        const auto hint=std::find_if(std::begin(OptionHints),std::end(OptionHints),[option](const auto& text) {return text.option==option;});
        constexpr Translation activeProfile{"This profile holds your current progress.","Ce profil contient votre progression actuelle.",
            "Dieses Profil enthaelt den aktuellen Fortschritt.","Questo profilo contiene i progressi attuali.",
            "Este perfil contiene tu progreso actual.","Den har profilen innehaller dina aktuella framsteg."};
        const auto row=offset_+highlightedRow_;
        const bool active=option==Row::Profile && row<profileNames_.size() &&
            profileNames_[row]==state_->services.profiles->ActiveProfile();
        blurb_.NewText(active?activeProfile[Language()]:hint!=std::end(OptionHints)?hint->text[Language()]:"");
        arrows_.SetCurrentItem(editable && page_!=PcPage::Options && !SliderSteps(option)?ValueKey(highlightedRow_):0);
    }
    void UpdateDeleteHint()
    {
        if(page_!=PcPage::Profiles)return;
        auto& services=state_->services;
        auto& resources=*services.ui->resources;
        const auto row=offset_+highlightedRow_;
        const bool available=row<profileNames_.size() && profileNames_[row]!=services.profiles->ActiveProfile();
        const auto icon=hashString("PC_DEL_ICON"),label=hashString("PC_DEL_TEXT");
        resources.SetObjectVisibility(state_->screen,icon,available);
        resources.SetObjectVisibility(state_->screen,label,available);
        if(!available)return;
        const char* glyph=resources.PromptDevice()==platform::NativePromptDevice::Keyboard?"\x9c":"\x88";
        constexpr Translation caption{"Delete","Supprimer","Loeschen","Elimina","Eliminar","Ta bort"};
        const auto setChanged=[&](std::uint32_t key,const char* text) {
            const auto* current=static_cast<const FlailerTextChunk*>(resources.FindObject(state_->screen,key,1));
            if(current && std::strcmp(current->text,text))resources.SetLocalizedText(state_->screen,key,text);
        };
        setChanged(icon,glyph);setChanged(label,caption[Language()]);
    }
    void BeginProfileEditor(std::size_t row,std::uint32_t key)
    {
        auto& services=state_->services;
        auto& resources=*services.ui->resources;
        resources.CloneProfileKeyboard(state_->screen);
        const auto* current=resources.FindObject(state_->screen,key,1);
        Require(current,"Profile name has no selected row");
        profileEditor_=std::make_unique<NativeProfileNameEditor>(services,state_->screen,current->topLeftY,row>=4);
        profileGridVisible_=false;
        arrows_.SetCurrentItem(0);
        RefreshProfileEditor();
    }
    void RefreshProfileEditor()
    {
        const bool visible=profileEditor_->GridVisible();
        if(visible!=profileGridVisible_) {
            profileGridVisible_=visible;
            Redraw();
            if(visible) {
                auto& resources=*state_->services.ui->resources;
                for(std::size_t row=0;row<RowCount();++row) {
                    if(highlightedRow_>=4?row>=4:row<5)continue;
                    resources.SetObjectVisibility(state_->screen,0x52e039f3u+static_cast<std::uint32_t>(row),0);
                    resources.SetObjectVisibility(state_->screen,ValueKey(row),0);
                }
            }
        }
        constexpr Translation picking{"Choose letters. Select Done to create.","Choisissez les lettres, puis Terminer.",
            "Buchstaben waehlen, dann Fertig.","Scegli le lettere, poi Fine.","Elige las letras y pulsa Listo.","Valj bokstaver och sedan Klar."};
        constexpr Translation typing{"Type the name. Enter creates it. Esc cancels.","Saisissez le nom. Entree confirme. Echap annule.",
            "Namen eingeben. Enter bestaetigt. Esc bricht ab.","Scrivi il nome. Invio crea. Esc annulla.",
            "Escribe el nombre. Intro crea. Esc cancela.","Skriv namnet. Enter skapar. Esc avbryter."};
        blurb_.NewText((visible?picking:typing)[Language()]);
    }
    void HandleEvent(ScreenMessage message,ScreenCallbackData& data) override
    {
        auto& services=state_->services;
        if(profileEditor_) {
            const bool previousGrid=profileEditor_->GridVisible();
            const auto result=profileEditor_->HandleEvent(message,data);
            std::string created;
            if(result==NativeProfileNameEditor::Result::Accept) {
                created=profileEditor_->Name();
                while(!created.empty() && (created.back()==' ' || created.back()=='.'))created.pop_back();
                if(!services.profiles->CreateProfile(created)) {ReportError(services.profiles->LastError());return;}
            }
            if(result!=NativeProfileNameEditor::Result::Editing) {
                profileEditor_.reset();RefreshProfiles();
                const auto profile=std::find(profileNames_.begin(),profileNames_.end(),created);
                if(profile!=profileNames_.end()) {
                    const auto index=static_cast<std::size_t>(profile-profileNames_.begin());
                    const auto first=index+1>RowCount()?index+1-RowCount():0;
                    const auto offset=std::clamp(offset_,first,index);
                    if(offset_!=offset) {offset_=offset;RefreshProfiles();}
                    auto* screen=services.screens->active->FindFlailerScreen(static_cast<std::int32_t>(state_->screen));
                    if(screen && screen->menu)screen->menu->SetActiveMenuItem(0x52e039f3u+static_cast<std::uint32_t>(index-offset_));
                }
                ShowHint(OptionAt(offset_+std::min(highlightedRow_,RowCount()-1)));
            } else if(previousGrid!=profileEditor_->GridVisible())RefreshProfileEditor();
            if(message==ScreenMessage::Tick)blurb_.Animate();
            return;
        }
        if(message==ScreenMessage::LeaveScreen && page_==PcPage::Profiles && services.profiles->ChoosingAtStartup()) {
            data.value=0;data.sound=0;
            RequestNativeMenuExit(services);return;
        }
        if(message==ScreenMessage::PopupDismiss) {
            if(data.value==NativeMenuExitResult) {applicationDebugExitRequested=true;return;}
            if(!deleteProfile_.empty()) {
                auto profile=std::move(deleteProfile_);deleteProfile_.clear();
                if(data.value==1) {
                    const bool deleted=services.profiles->DeleteProfile(profile);
                    RefreshProfiles();
                    services.screens->active->SetActiveMenuItem(0x52e039f3u);
                    if(!deleted)ReportError(services.profiles->LastError());
                }
            }
            return;
        }
        if(message==ScreenMessage::Tick) {
            UpdateDeleteHint();
            if((page_==PcPage::Gamepad || page_==PcPage::PlayerTwo) && hasDualSense_!=host_.HasDualSense(Port()))Redraw();
            if(capture_>=0)PollBinding();
            if(page_==PcPage::PlayerTwo) {
                const auto name=host_.InputDeviceName(Port());
                if(name!=deviceName_) {deviceName_=name;Redraw();}
            }
            for(auto& slider:sliders_)if(slider)slider->Animate();
            blurb_.Animate();arrows_.Animate();return;
        }
        const auto row=data.key-0x52e039f3u;
        if(row>=RowCount())return;
        if((message==ScreenMessage::Next && row+1==RowCount() && offset_+RowCount()<OptionCount()) ||
           (message==ScreenMessage::Previous && row==0 && offset_>0)) {
            if(message==ScreenMessage::Next)++offset_;
            else --offset_;
            data.value=1;
            Redraw();
            ShowHint(OptionAt(offset_+row));
            services.ui->sound->Play(data.sound);
            return;
        }
        if((message==ScreenMessage::Next && row+1==RowCount() && offset_>0) ||
           (message==ScreenMessage::Previous && row==0 && OptionCount()>RowCount())) {
            offset_=message==ScreenMessage::Next?0:OptionCount()-RowCount();
            Redraw();
        }
        const auto option=OptionAt(offset_+row);
        if(message==ScreenMessage::ItemEvent && data.value==17 && option==Row::Profile) {
            AskDelete(profileNames_[offset_+row],data.joystickPort);return;
        }
        if(message==ScreenMessage::Highlight) {
            highlightedRow_=row;ShowHint(option);
            return;
        }
        if(SliderSteps(option) && (message==ScreenMessage::Left || message==ScreenMessage::Right ||
            message==ScreenMessage::Select || message==ScreenMessage::SliderDrag || message==ScreenMessage::SliderCommit)) {
            AdjustSlider(row,option,message,data);return;
        }
        const bool selected=message==ScreenMessage::Select;
        if(!selected && message!=ScreenMessage::Left && message!=ScreenMessage::Right)return;
        const int direction=message==ScreenMessage::Left?-1:1;
        if(selected) {
            if(option>=Row::English && option<=Row::Swedish) {
                services.ui->language->SetLanguage(static_cast<int>(option)-static_cast<int>(Row::English));
                Redraw();
                if(services.profiles->SaveProfile())ShowHint(option);
                else ReportError(services.profiles->LastError());
                return;
            }
            switch(option) {
            case Row::Audio:services.screens->ChangeToScreen("Audio Options",true);return;
            case Row::Controls:OpenPcPage(services,PcPage::Controls);return;
            case Row::Graphics:OpenPcPage(services,PcPage::Graphics);return;
            case Row::Profiles:OpenPcPage(services,PcPage::Profiles);return;
            case Row::Languages:OpenPcPage(services,PcPage::Languages);return;
            case Row::Credits:services.screens->ChangeToScreen("Credits",true);return;
            case Row::ResolutionPage:OpenPcPage(services,PcPage::Resolution);return;
            case Row::Advanced:OpenPcPage(services,PcPage::Advanced);return;
            case Row::AntialiasPage:OpenPcPage(services,PcPage::Antialiasing);return;
            case Row::Keyboard:OpenPcPage(services,page_==PcPage::PlayerTwo?PcPage::KeyboardTwo:PcPage::Keyboard);return;
            case Row::Gamepad:OpenPcPage(services,PcPage::Gamepad);return;
            case Row::Player:OpenPcPage(services,PcPage::PlayerTwo);return;
            case Row::Layouts:services.screens->ChangeToScreen("Joystick PreConfig",true);return;
            case Row::Hud:gDisplayOptionsChangeManager.Start(*services.frontend);services.screens->ChangeToScreen("Display Options",true);return;
            case Row::SaveLoad:services.screens->ChangeToScreen("Phils Load Save",true);return;
            case Row::Name:services.screens->ChangeToScreen("NameEntry",true);return;
            case Row::NewProfile:
                BeginProfileEditor(row,data.key);
                return;
            case Row::Profile: {
                const bool startup=services.profiles->ChoosingAtStartup();
                if(!services.profiles->SelectProfile(profileNames_[offset_+row])) {
                    ReportError(services.profiles->LastError());return;
                }
                if(startup)services.screens->MoveToPreviousScreen(1,false,data.joystickPort);
                else {RefreshProfiles();ShowHint(option);}
                return;
            }
            default:break;
            }
            if(BindingIndex(option)>=0) {
                capture_=static_cast<int>(offset_+row);
                host_.BeginBindingCapture(Port());
                constexpr Translation waiting{"Press a key. Esc cancels.","Nouvelle touche. Echap annule.",
                    "Neue Taste druecken. Esc bricht ab.","Premi un tasto. Esc annulla.",
                    "Pulsa una tecla. Esc cancela.","Tryck en knapp. Esc avbryter."};
                blurb_.NewText(waiting[Language()]);Redraw();return;
            }
        }
        auto settings=*host_.PcSettings();
        bool display=false;
        switch(option) {
        case Row::Resolution: {
            auto modes=host_.MenuDisplayModes();
            if(settings.fullscreen) {
                modes.insert(modes.begin(),{0,0});
                settings.fullscreenSize=CycleDisplayChoice(settings.fullscreenSize,modes,direction);
            } else {
                const auto size=CycleDisplayChoice({settings.width,settings.height},modes,direction);
                settings.width=size.first;settings.height=size.second;
            }
            display=true;break;
        }
        case Row::Window:settings.fullscreen=!settings.fullscreen;display=true;break;
        case Row::Vsync:settings.vsync=!settings.vsync;display=true;break;
        case Row::FrameLimit: {
            constexpr int limits[]{0,30,40,45,50,60,75,90,100,120,144,165,180,200,240,300,360,480};
            settings.frameLimit=Cycle(settings.frameLimit,limits,direction);
            break;
        }
        case Row::Aspect:
            settings.raceAspect=CycleDisplayChoice(settings.raceAspect,nativeAspectChoices(host_.MenuDisplayModes()),direction);
            display=true;break;
        case Row::Scaling:settings.sharpScaling=!settings.sharpScaling;break;
        case Row::RenderScale: {
            const auto scale=std::clamp(settings.renderScale+direction,0,8);
            if(scale==settings.renderScale) {data.sound=0;return;}
            settings.renderScale=scale;
            break;
        }
        case Row::Anisotropy: {
            const auto current=settings.textureFilter==NativeTextureFilter::Anisotropic?settings.anisotropy:0;
            const auto size=Cycle(current,PcAnisotropy,direction);
            settings.textureFilter=size?NativeTextureFilter::Anisotropic:NativeTextureFilter::Trilinear;
            if(size)settings.anisotropy=size;
            break;
        }
        case Row::Detail:settings.render.maximumDetail=!settings.render.maximumDetail;break;
        case Row::Reflections:settings.render.reflections=(settings.render.reflections+direction+3)%3;break;
        case Row::Antialiasing: {
            constexpr NativeAntialiasing modes[]{NativeAntialiasing::Off,NativeAntialiasing::Msaa2,NativeAntialiasing::SmaaHigh};
            const auto old=settings.antialiasing;
            const auto mode=old>=NativeAntialiasing::SmaaHigh?NativeAntialiasing::SmaaHigh:
                old>=NativeAntialiasing::Msaa2?NativeAntialiasing::Msaa2:old;
            settings.antialiasing=Cycle(mode,modes,direction);break;
        }
        case Row::Quality:
            if(settings.antialiasing<NativeAntialiasing::Msaa2) {data.sound=0;return;}
            if(settings.antialiasing>=NativeAntialiasing::SmaaHigh)
                settings.antialiasing=settings.antialiasing==NativeAntialiasing::SmaaHigh?
                    NativeAntialiasing::SmaaUltra:NativeAntialiasing::SmaaHigh;
            else settings.antialiasing=settings.antialiasing==NativeAntialiasing::Msaa2?
                    NativeAntialiasing::Msaa4:NativeAntialiasing::Msaa2;
            break;
        case Row::PictureDefaults:
            if(!selected)return;
            settings.render.brightness=0;settings.render.contrast=settings.render.gamma=100;break;
        case Row::Fps:settings.showFps=!settings.showFps;break;
        case Row::AdaptiveTriggers: {
            if(!host_.HasDualSense(Port()))return;
            auto& enabled=settings.input.profiles[Port()].adaptiveTriggers;
            enabled=!enabled;break;
        }
        case Row::PoliceRadioSpeaker: {
            if(!host_.HasDualSense(Port()))return;
            auto& enabled=settings.input.profiles[Port()].policeRadioSpeaker;
            enabled=!enabled;break;
        }
        case Row::IconStyle:settings.input.promptStyle=(settings.input.promptStyle+direction+3)%3;break;
        case Row::InputMode: {
            auto& mode=settings.input.deviceMode[Port()];
            constexpr auto count=static_cast<int>(platform::NativeDeviceMode::Disabled)+1;
            mode=static_cast<platform::NativeDeviceMode>((static_cast<int>(mode)+direction+count)%count);
            break;
        }
        case Row::Player2Pad:
            if(settings.input.deviceMode[1]==platform::NativeDeviceMode::Keyboard ||
                settings.input.deviceMode[1]==platform::NativeDeviceMode::Disabled) {data.sound=0;return;}
            std::swap(settings.input.controller[0],settings.input.controller[1]);
            std::swap(settings.input.deviceKey[0],settings.input.deviceKey[1]);break;
        case Row::PadDefaults:
            if(!selected)return;
            settings.input.profiles[0]=platform::NativeGamepadProfile{};
            settings.input.steeringSensitivity=100;settings.input.promptStyle=0;break;
        case Row::KeyboardDefaults:
            if(!selected)return;
            if(Port()==0)settings.input.keys=platform::NativeInputSettings{}.keys;
            else settings.input.player2Keys=platform::NativeInputSettings{}.player2Keys;
            settings.input.zoneKeys[Port()]=platform::NativeInputSettings{}.zoneKeys[Port()];
            break;
        default:data.sound=0;return;
        }
        Save(settings,display);
    }
    void PollBinding()
    {
        auto settings=*host_.PcSettings();
        const auto option=OptionAt(capture_);
        const auto index=static_cast<std::size_t>(BindingIndex(option));
        const auto key=host_.CapturedKey();
        if(!key)return;
        capture_=-1;host_.BeginBindingCapture(-1);
        ShowHint(option);
        if(key==0x1b) {Redraw();return;}
        auto& keys=Port()==0?settings.input.keys:settings.input.player2Keys;
        auto& zones=settings.input.zoneKeys[Port()];
        auto& binding=index>=20?zones[index-20]:keys[index];
        const auto swapKey=[&](std::uint16_t& candidate) {
            if(&candidate!=&binding && candidate==key)candidate=binding;
        };
        for(std::size_t i=0;i<18;++i)swapKey(keys[i]);
        for(auto& zone:zones)swapKey(zone);
        binding=key;
        Save(settings,false);
    }
    PcPage page_;
    const PcPageDescription& description_;
    NativeApplicationHost& host_;
    FlailerAnimatedBlurb blurb_;
    FlailerLeftRightArrowScroll arrows_;
    std::size_t offset_{},highlightedRow_{};
    std::vector<std::string> profileNames_;
    std::int32_t capture_{-1};
    std::string deleteProfile_;
    std::unique_ptr<NativeProfileNameEditor> profileEditor_;
    bool profileGridVisible_{};
    bool renderPreview_{};
    std::array<float,7> labelScale_{},valueScale_{};
    std::array<std::optional<FlailerSlider>,VisiblePcRows> sliders_;
    std::string deviceName_;
    bool hasDualSense_{},radioPreview_{};
};
} // namespace

std::array<FlailerScreenConfig,11>& NativeOptionsScreenConfigurations()
{
    static constexpr std::int32_t Help[]{800057,800058,800059,0};
    static auto configurations=[] {
        std::array<FlailerScreenConfig,11> result{};
        for(std::size_t i=0;i<result.size();++i) {
            result[i].name=PcPages[i].name;
            result[i].callback=&NativeOptionsScreen::Callback;
            result[i].screenCount=1;
            result[i].screenIds[0]=static_cast<std::int32_t>(hashString(PcPages[i].name));
            result[i].helpItems=Help;
        }
        return result;
    }();
    return configurations;
}

void OpenStartupProfileMenu(MainMenuScreenServices& services)
{
    ClonePcPages(services);
    OpenPcPage(services,PcPage::Profiles);
}

OptionsScreen::OptionsScreen(MainMenuScreenServices& services,std::uint32_t screen)
    : OptionsScreenBase(services,screen),
      blurb_(std::make_unique<FlailerAnimatedBlurb>(*services.ui,screen,0x75980cf0u,0x75980cf0u,0x70a404ecu))
{
    // PAL1EEB78 initializes only the embedded RumbleTimer word. Its Tick
    // and TriggerSomething callees are final JR RA/NOP and are not emitted.
    rumbleTimer_=0;

    if (gameTimeGetVideoMode(services.ui->clock)==1) {
        // Complete relevant constant-state2 path of actual16D198: find the
        // registered screen/menu/item, clear enabled bit1 and hide its object.
        // No replacement menu or generic callback is created for this call.
        if (auto* node=services.screens->FindScreen(static_cast<std::int32_t>(screen)))
            if (auto* sourceScreen=node->FindFlailerScreen(static_cast<std::int32_t>(screen)))
                if (sourceScreen->menu)
                    if (auto* item=sourceScreen->menu->FindItem(0x52e039f9u)) {
                        item->ClearMenuItemState(1);
                        services.ui->resources->SetObjectVisibility(screen,0x52e039f9u,0);
                    }
    }
}
OptionsScreen::~OptionsScreen()=default;

void OptionsScreen::HandleEvent(ScreenMessage message,ScreenCallbackData& data)
{
    auto& state=*state_;
    if (message==ScreenMessage::Select) {
        const char* destination=""; // actual PAL331660 default for unknown key
        switch (data.key) {
        case 0x52e039f3u:destination="Audio Options";break;
        case 0x52e039f4u:destination="Joystick PreConfig";break;
        case 0x52e039f5u:
            destination="Display Options";
            gDisplayOptionsChangeManager.Start(*state.services.frontend);
            break;
        case 0x52e039f6u:state.services.screens->ChangeToScreen("Phils Load Save",true);return;
        case 0x52e039f7u:destination="NameEntry";break;
        // Final jump table2B9BD0 selects Credits for F8, Language for F9;
        // physical ordering of their string/basic blocks is different.
        case 0x52e039f8u:state.services.screens->ChangeToScreen("Credits",true);return;
        case 0x52e039f9u:state.services.screens->ChangeToScreen("Language Options",true);return;
        default:break;
        }
        Require(!state.exit,"Options already owns an exit animation");
        auto& ui=*state.services.ui;
        state.exit=std::make_unique<FlailerExitScreenAnimation>(ui,*state.services.screens,
            state.screen,destination,true);
        auto* anchor=ui.resources->FindObject(state.screen,0x32c91daeu);
        Require(anchor,"Options exit has no authored lower rail anchor");
        float x,y;ui.resources->GetTopLeft(*anchor,x,y,ui.language);
        state.exit->AddGroup(0x97bf4b25u,0x32c91daeu,x,650.0f,state.exit->MaximumDuration());
        return;
    }
    if (message==ScreenMessage::Highlight) {
        std::int32_t language=0;
        if (data.key>=0x52e039f3u && data.key<=0x52e039f9u)
            language=100020+static_cast<std::int32_t>(data.key-0x52e039f3u);
        blurb_->NewText(language);
        // Original Highlight falls through to the same Animate operation.
    } else if (message!=ScreenMessage::Tick)return;
    blurb_->Animate();
    // Rumbleator.Tick PAL1EEBA8 is a proven no-op, not a sound/input stub.
}

void OptionsScreen::Callback(ScreenMessage message,ScreenCallbackData* data)
{
    if (message==ScreenMessage::ScreenActivate) {
        Require(optionsServices && data && !optionsOwner,
            "Options activation lacks actual services/data or duplicates a live owner");
        if(optionsServices->pcHost) {
            ClonePcPages(*optionsServices);
            Install(std::make_unique<NativeOptionsScreen>(*optionsServices,data->key,PcPage::Options));
            return;
        }
        optionsOwner=std::make_unique<OptionsScreen>(*optionsServices,data->key);
    } else OptionsScreenBase::BaseCallback(message,data);
}

void BindOptionsScreen(MainMenuScreenServices& services)
{
    Require(!optionsOwner && services.ui && services.screens && services.frontend,
        "Options services cannot replace a live screen or omit original owners");
    optionsServices=&services;
}
void UnbindOptionsScreen()
{
    ClearLanguageOptionsOwner();optionsOwner.reset();optionsServices=nullptr;
}
namespace {
constexpr std::int32_t OptionsHelpItems[]={800057,800058,800059,0};
}
FlailerScreenConfig fOptions={
    "Options",&OptionsScreen::Callback,
    {{static_cast<std::int32_t>(0x95ad096bu),0,0,0}},{{-1,-1,-1,-1}},1,OptionsHelpItems};

} // namespace hp2::frontend

#include "bsound/include/bJukebox.h"
#include "bsound/include/bSound.h"
#include "CarSounds.hpp"
#include <cstring>
namespace hp2::frontend {
namespace {
// Full original GenreMgr::Populate1EF240. Genre rows are the screen's
// bounded derived index; the actual playlist remains in FRED and cJukebox.
class GenreMgr {
public:
    struct Genre {
        std::array<char,20> name{};
        std::int32_t count{};
        std::array<std::uint8_t,32> rows{};
    };
    std::array<Genre,6> genres{};
    std::int32_t count{};
    void Populate(const CarSoundBindings& sound,FrontendMusicSettings& settings)
    {
        genres={};count=0;
        Require(settings.count>=0 && static_cast<std::size_t>(settings.count)<=settings.rows.size(),
            "Music row count exceeds the original settings table");
        for(std::int32_t index=0;index<settings.count;++index) {
            auto& row=settings.rows[static_cast<std::size_t>(index)];
            const auto* name=soundJukeboxGetTrackGenre(sound,row.group,row.track);
            if(!name || !*name)continue;
            std::int32_t genre=0;
            for(;genre<count;++genre)
                if(std::strcmp(name,genres[static_cast<std::size_t>(genre)].name.data())==0)break;
            if(genre==count) {
                Require(static_cast<std::size_t>(count)<genres.size(),
                    "Final jukebox exceeds the original six genres");
                auto& destination=genres[static_cast<std::size_t>(genre)].name;
                const auto bytes=std::strlen(name);
                Require(bytes<destination.size(),"Final jukebox genre exceeds its original name slot");
                std::memcpy(destination.data(),name,bytes+1);++count;
            }
            auto& group=genres[static_cast<std::size_t>(genre)];
            Require(static_cast<std::size_t>(group.count)<group.rows.size(),
                "Final jukebox genre exceeds its original track-index slots");
            row.genreBit=static_cast<std::int8_t>(genre);
            group.rows[static_cast<std::size_t>(group.count++)]=static_cast<std::uint8_t>(index);
        }
    }
};
}

void FrontendPlayerManager::CaptureAudioOptions(const CarSoundBindings& sound,bool rebuildMusicList)
{
    Require(sound.audio && sound.audio->library,"Audio options have no actual native mixer");
    constexpr std::array<std::uint32_t,7> categories{{5,4,0,1,2,3,6}};
    for(std::size_t i=0;i<categories.size();++i)
        musicSettings.savedVolumes[i]=sound.audio->library->GetCategoryVolume(categories[i]);
    musicSettings.savedAudioMode=worldAudioGetMode();
    if(!rebuildMusicList)return;
    // PAL1EEF60 iterates the actual loaded cartridge's groups/tracks and
    // retains at most32 rows. PAL1EF240 assigns genre bits in first-seen
    // string order; strings come from the same final jukebox metadata cache.
    musicSettings.count=0;
    for(std::size_t group=0;group<soundJukeboxPlayer.groups.size();++group) {
        const auto& row=soundJukeboxPlayer.groups[group];
        for(std::size_t track=0;track<row.tunes.size();++track) {
            if(musicSettings.count>=static_cast<std::int32_t>(musicSettings.rows.size()))continue;
            Require(group<=127 && track<=127,"Jukebox indices exceed original signed row lanes");
            auto& output=musicSettings.rows[static_cast<std::size_t>(musicSettings.count++)];
            output.group=static_cast<std::int8_t>(group);output.track=static_cast<std::int8_t>(track);
            output.status=static_cast<std::int8_t>(soundJukeboxGetTrackStatus(sound,static_cast<std::int32_t>(group),static_cast<std::int32_t>(track)));

        }
    }
    // PAL DataAudioOptionsScreen::InitWithDefaults1EF1C8 calls the
    // actual capture, initializes A0/A4/A8/AE/AF, then populates GenreMgr.
    musicSettings.menuState={{0,5,0}};musicSettings.menuMode=0;musicSettings.enabledGenres=255;
    GenreMgr genres;genres.Populate(sound,musicSettings);
}

void FrontendPlayerManager::AppendMissingMusicTracks(const CarSoundBindings& sound)
{
    if(!soundJukeboxPlayer.IsCartridgeLoaded())return;
    Require(musicSettings.count>=0 && static_cast<std::size_t>(musicSettings.count)<=musicSettings.rows.size(),
        "Saved playlist exceeds the native music rows");
    const auto previousCount=musicSettings.count;
    const auto favourite=musicSettings.menuState[1];
    std::int32_t retained=0,newFavourite=0;
    for(std::int32_t i=0;i<previousCount;++i) {
        const auto row=musicSettings.rows[i];
        const bool present=row.group>=0 && static_cast<std::size_t>(row.group)<soundJukeboxPlayer.groups.size() &&
            row.track>=0 && static_cast<std::size_t>(row.track)<soundJukeboxPlayer.groups[row.group].tunes.size();
        if(!present)continue;
        if(i==favourite)newFavourite=retained;
        musicSettings.rows[retained++]=row;
    }
    musicSettings.count=retained;
    for(std::size_t i=retained;i<musicSettings.rows.size();++i)musicSettings.rows[i]={};
    if(retained!=previousCount){musicSettings.menuState[0]=0;musicSettings.menuState[1]=newFavourite;musicSettings.menuMode=0;}
    for(std::size_t group=0;group<soundJukeboxPlayer.groups.size();++group)
        for(std::size_t track=0;track<soundJukeboxPlayer.groups[group].tunes.size();++track) {
            bool present=false;
            for(std::int32_t i=0;i<musicSettings.count;++i)
                if(musicSettings.rows[i].group==group && musicSettings.rows[i].track==track){present=true;break;}
            if(present)continue;
            Require(musicSettings.count>=0 && static_cast<std::size_t>(musicSettings.count)<musicSettings.rows.size(),
                "Saved playlist exceeds its native track storage");
            auto& row=musicSettings.rows[musicSettings.count++];
            row.group=static_cast<std::int8_t>(group);row.track=static_cast<std::int8_t>(track);
            row.status=static_cast<std::int8_t>(soundJukeboxGetTrackStatus(sound,row.group,row.track));
        }
    GenreMgr genres;genres.Populate(sound,musicSettings);

}

void FrontendPlayerManager::ApplyAudioOptions(const CarSoundBindings& bindings,
    const RaceParameters& parameters,FlailerSoundState& uiSound) const
{
    Require(bindings.audio && bindings.audio->library && uiSound.audio==bindings.audio &&
        uiSound.manager==&bindings.audio->library->Mixer() && uiSound.volumes &&
        bindings.volumes==uiSound.volumes,"Saved audio options have no shared canonical mixer and speech-volume owner");
    Require(nativeAudioProfileValid(musicSettings,applicationLaunchOptions.musicEnabled?&soundJukeboxPlayer:nullptr),
        "Saved audio options must pass validation before changing the live mixer");
    Require(!applicationLaunchOptions.musicEnabled || soundJukeboxPlayer.audio==bindings.audio,
        "Saved playlist has no shared canonical jukebox owner");
    // Complete final SetSoundFromAudioOptions1EF120. Category order is
    // source5,4,0,6,2,3,1, followed by speech-volume save, playlist and mode.
    constexpr std::array<std::uint32_t,7> categories{{5,4,0,6,2,3,1}};
    constexpr std::array<std::size_t,7> savedIndices{{0,1,2,6,4,5,3}};
    for(std::size_t i=0;i<categories.size();++i)
        Require(bindings.audio->library->SetCategoryVolume(categories[i],musicSettings.savedVolumes[savedIndices[i]])==WORLD_AUDIO_OK,
            "Physical mixer rejected a saved category volume");
    sound::SaveVolumesForSpeech(uiSound);
    Require(soundJukeboxSetSelection(bindings,*this,parameters,5)==WORLD_AUDIO_OK,
        "Actual jukebox rejected the saved track and genre choices");
    Require(worldAudioSetMode(bindings.audio,musicSettings.savedAudioMode)==WORLD_AUDIO_OK,
        "Physical mixer rejected the saved audio mode");
}

} // namespace hp2::frontend

#include "Language.hpp"
namespace hp2::frontend {
namespace {
// The unshipped bulk-language debug command is intentionally not exposed
// by the native PC frontend; ordinary language selection stays unchanged.
class LanguageOptionsScreenBase final {
public:
    LanguageOptionsScreenBase(MainMenuScreenServices& services,std::uint32_t screen,bool preFrontend)
        :services_(services),screen_(screen),preFrontend_(preFrontend)
    {
        Require(services.ui && services.ui->language && services.screens && services.frontend && services.raceSetup,
            "Language options lack their canonical runtime owners");
        selected_=std::max(services.ui->language->currentLanguage,0);
        if(!preFrontend)slide_=std::make_unique<FlailerSlideOnScreen>(*services.ui,screen,
            0x03c790bbu,0x718cd8c3u,0x95ad096bu);
        // FRONTB's seventh choice is "Load All Languages", an authored
        // debug-only operation. There is no normal PAL gameplay consumer for
        // bulk loading; leaving it selectable causes an invalid screen exit.
        // Keep ordinary six language choices independent of this debug item.
        // This authored bulk-load action is not a released gameplay option.
        // Clearing the enabled bit (rather than setting the highlight bit)
        // actually prevents navigation to the invalid debug-only callback.
        if(auto* node=services.screens->active) {
            for(auto* current=node->screenRoot.next;current!=&node->screenRoot;current=current->next)
                if(current->menu)
                    if(auto* item=current->menu->FindItem(0x52e039f9u))
                        item->ClearMenuItemState(1);
        }
        services.ui->resources->SetObjectVisibility(screen,0x52e039f9u,0);
    }
    void HandleScreenCallback(ScreenMessage message,ScreenCallbackData& data) {
        if(message==ScreenMessage::Tick){if(slide_)slide_->Tick();return;}
        if(message==ScreenMessage::LeaveScreen) {
            data.value=0;
            if(services_.ui->language->currentLanguage==-1)return;
            auto* active=services_.screens->active;
            const auto choice=active?active->GetActiveMenuItem():0;
            if(choice>=0x52e039f3u && choice<=0x52e039f8u) {
                selected_=static_cast<std::int32_t>(choice-0x52e039f3u);CommitLanguage();
            }
            Require(services_.screens->MoveToPreviousScreen(1,false,data.joystickPort),
                "Language options have no preceding original screen");return;
        }
        if(message!=ScreenMessage::Select)return;
        if(data.key>=0x52e039f3u && data.key<=0x52e039f8u) {
            selected_=static_cast<std::int32_t>(data.key-0x52e039f3u);CommitLanguage();
            Require(services_.screens->MoveToPreviousScreen(1,false,data.joystickPort),
                "Committed language has no preceding original screen");return;
        }
        if(data.key==0x52e039f9u) {
            // A synthetic selection event must not reactivate the debug-only
            // action even when native developer diagnostics were enabled.
            data.sound=0;
            return;
        }
    }
private:
    void CommitLanguage() {
        services_.ui->language->SetLanguage(selected_);
        if(preFrontend_ && services_.ui->language->currentLanguage!=-1) {
            services_.frontend->InitWithDefaults(*services_.raceSetup);
            if(services_.sounds)services_.frontend->CaptureAudioOptions(*services_.sounds,true);
        }
    }
    MainMenuScreenServices& services_;
    std::uint32_t screen_{};
    bool preFrontend_{};
    std::int32_t selected_{};
    std::unique_ptr<FlailerSlideOnScreen> slide_;
};
std::unique_ptr<LanguageOptionsScreenBase> languageOptionsOwner;
void ClearLanguageOptionsOwner(){languageOptionsOwner.reset();}
constexpr std::array<std::int32_t,4> LanguageHelp{{0xc354c,0xc354d,0xc354e,0}};
}
void LanguageOptionsScreenCallback(ScreenMessage message,ScreenCallbackData* data)
{
    Require(optionsServices && data,"Language callback lacks the actual options binding/payload");
    if(message==ScreenMessage::ScreenActivate) {
        Require(!languageOptionsOwner,"Language options received duplicate activation");
        auto* node=optionsServices->screens->active;
        Require(node && node->configuration && node->configuration->name,"Language screen lacks its original configuration");
        const bool pre=!std::strcmp(node->configuration->name,"Language Options Pre FE");
        languageOptionsOwner=std::make_unique<LanguageOptionsScreenBase>(*optionsServices,data->key,pre);
    } else if(message==ScreenMessage::ScreenDeactivate)languageOptionsOwner.reset();
    else {
        Require(languageOptionsOwner!=nullptr,"Language message arrived outside its actual screen lifetime");
        languageOptionsOwner->HandleScreenCallback(message,*data);
    }
}
std::array<FlailerScreenConfig,2>& LanguageScreenConfigurations()
{
    // Exact final1F5738..1F578C constructor arguments, source names and shared
    // callback. Both configurations borrow the one final authored screen.
    static std::array<FlailerScreenConfig,2> configurations{{
        {"Language Options",LanguageOptionsScreenCallback,{{static_cast<std::int32_t>(0x632b068eu),0,0,0}},{{-1,-1,-1,-1}},1,LanguageHelp.data()},
        {"Language Options Pre FE",LanguageOptionsScreenCallback,{{static_cast<std::int32_t>(0x632b068eu),0,0,0}},{{-1,-1,-1,-1}},1,nullptr}
    }};
    return configurations;
}
} // namespace hp2::frontend

#include "DemoDisc.hpp"
#include "GameFlow.hpp"
namespace hp2::frontend {
namespace {
constexpr std::uint32_t AudioChannels63[7]={5,4,0,1,2,3,6};
constexpr Translation AudioVolumeHints[]{
    {"Set the music volume in menus.","Regler le volume de la musique des menus.","Lautstaerke der Menue-Musik einstellen.",
        "Regola il volume della musica nei menu.","Ajusta el volumen de la musica en los menus.","Stall in musikvolymen i menyerna."},
    {"Set the music volume during races.","Regler le volume de la musique en course.","Lautstaerke der Rennmusik einstellen.",
        "Regola il volume della musica in gara.","Ajusta el volumen de la musica en carrera.","Stall in musikvolymen under lopp."},
    {"Set the volume of sounds around the track.","Regler le volume des sons du circuit.","Lautstaerke der Streckengeraeusche einstellen.",
        "Regola il volume dei suoni del circuito.","Ajusta el volumen de los sonidos de la pista.","Stall in volymen for ljud runt banan."},
    {"Set the volume of tire sounds.","Regler le volume du bruit des pneus.","Lautstaerke der Reifengeraeusche einstellen.",
        "Regola il volume dei suoni degli pneumatici.","Ajusta el volumen del ruido de los neumaticos.","Stall in volymen for dackljud."},
    {"Set the volume of engine sounds.","Regler le volume du bruit des moteurs.","Lautstaerke der Motorengeraeusche einstellen.",
        "Regola il volume dei suoni dei motori.","Ajusta el volumen del ruido de los motores.","Stall in volymen for motorljud."},
    {"Set the volume of menu navigation sounds.","Regler le volume des sons de navigation.","Lautstaerke der Menue-Geraeusche einstellen.",
        "Regola il volume dei suoni dei menu.","Ajusta el volumen de los sonidos del menu.","Stall in volymen for menyljud."},
    {"Set the volume of voices and radio calls.","Regler le volume des voix et appels radio.","Lautstaerke fuer Stimmen und Funk einstellen.",
        "Regola il volume delle voci e della radio.","Ajusta el volumen de las voces y la radio.","Stall in volymen for roster och radio."},
};
static_assert(std::size(AudioVolumeHints)==std::size(AudioChannels63));
constexpr std::uint32_t MenuFirst63=0x52e039f3u;
constexpr std::uint32_t MenuMaster=MenuFirst63-1u;

bool SameAudioOptions63(const FrontendMusicSettings& first,const FrontendMusicSettings& second)
{
    if(first.savedVolumes!=second.savedVolumes || first.savedAudioMode!=second.savedAudioMode ||
       first.count!=second.count || first.enabledGenres!=second.enabledGenres ||
       first.menuState!=second.menuState || first.menuMode!=second.menuMode)return false;
    for(std::size_t index=0;index<first.rows.size();++index) {
        const auto& a=first.rows[index];const auto& b=second.rows[index];
        if(std::tie(a.group,a.track,a.status,a.genreBit)!=std::tie(b.group,b.track,b.status,b.genreBit))return false;
    }
    return true;
}

class AudioOptionsScreen final:public OptionsScreenBase {
    FrontendMusicSettings editing_;
    std::array<std::unique_ptr<FlailerSlider>,7> sliders_;
    std::optional<FlailerSlider> master_;
    bool masterPreview_{};
    FlailerScrollingMenuList scrolling_;
    FlailerLeftRightArrowScroll arrows_;
    FlailerAnimatedBlurb blurb_;
public:
    AudioOptionsScreen(MainMenuScreenServices& services,std::uint32_t screen)
        :OptionsScreenBase(services,screen),editing_(services.frontend->musicSettings),
         scrolling_(*services.ui,screen,MenuFirst63,MenuFirst63+6,0xedf2e4eeu,0x20d73d01u,0xc67cfb4au),
         arrows_(*services.ui,screen,0xd1cda594u,0x9345e4a7u),
         blurb_(*services.ui,screen,0x75980cf0u,0x75980cf0u,0x70a404ecu)
    {
        Require(services.sounds && services.sounds->audio && services.sounds->audio->library,
            "Audio options require the original live mixer");
        auto& mixer=*services.sounds->audio->library;
        // PAL1EF3C8 copies FRED data, then reads the seven actual categories.
        for(std::size_t index=0;index<7;++index) {
            editing_.savedVolumes[index]=mixer.GetCategoryVolume(AudioChannels63[index]);
            sliders_[index]=std::make_unique<FlailerSlider>(*services.ui,screen,
                MenuFirst63+static_cast<std::uint32_t>(index),0xa29f7a16u+static_cast<std::uint32_t>(index),
                0xc866642bu+0x441u*static_cast<std::uint32_t>(index),
                static_cast<std::int32_t>(AudioChannels63[index]),&editing_.savedVolumes[index],0.0f,1.0f);
        }
        if(services.pcHost) {
            constexpr Translation name{"Master Volume","Volume general","Gesamtlautstaerke",
                "Volume generale","Volumen general","Huvudvolym"};
            services.ui->resources->SetLocalizedText(screen,MenuMaster,name[MenuLanguage(services)]);
            scrolling_.SetVisibleArea(MenuMaster,MenuFirst63+5);
            scrolling_.AddObject(MenuMaster);
            scrolling_.AddObject(hashString("PC_MASTER_BAR"));
            scrolling_.AddObject(hashString("PC_MASTER_MARKER"));
            master_.emplace(*services.ui,screen,MenuMaster,hashString("PC_MASTER_BAR"),hashString("PC_MASTER_MARKER"),
                static_cast<float>(services.pcHost->PcSettings()->masterVolume)*0.01f);
        }
        editing_.savedAudioMode=worldAudioGetMode();RedrawMode();
        constexpr std::uint32_t objects[]={0x52e039f3u,0x52e039f4u,0x52e039f5u,0x52e039f6u,
            0x52e039f7u,0x52e039f8u,0x52e039f9u,0x52e039fau,0x52e039fbu,0xaee77883u,0xaee77884u,
            0xa29f7a16u,0xc866642bu,0xa29f7a17u,0xc866686cu,0xa29f7a18u,0xc8666cadu,
            0xa29f7a19u,0xc86670eeu,0xa29f7a1au,0xc866752fu,0xa29f7a1bu,0xc8667970u,
            0xa29f7a1cu,0xc8667db1u,0x2932dfcau};
        scrolling_.AddObjects(objects,std::size(objects));scrolling_.SetItemCount(master_?12:11);
    }
    ~AudioOptionsScreen() override {EndMasterPreview();}
    void HandleHighlight(ScreenMessage,ScreenCallbackData&,std::uint32_t) override {}
    void EndMasterPreview() noexcept
    {
        if(std::exchange(masterPreview_,false))state_->services.pcHost->PreviewMasterVolume(std::nullopt);
    }
    void AdjustMaster(float fraction,bool drag,bool snap)
    {
        auto& host=*state_->services.pcHost;
        auto settings=*host.PcSettings();
        fraction=std::clamp(fraction,0.0f,1.0f);
        const auto percentage=static_cast<int>(std::lround(fraction*100.0f));
        const bool changed=master_->position.desiredValue!=fraction;
        if(drag) {
            host.PreviewMasterVolume(percentage);
            masterPreview_=true;
        } else EndMasterPreview();
        if(!drag && settings.masterVolume!=percentage) {
            settings.masterVolume=percentage;
            std::string error;
            if(!host.SavePcSettings(settings,error)) {
                host.RecordSessionEvent("audio.settings_failed",error.c_str());
                master_->ShowValue(host.PcSettings()->masterVolume*0.01f,true);
                return;
            }
        }
        master_->ShowValue(fraction,snap);
        if(changed)master_->PlayChange(fraction);
    }
    void RedrawMode()
    {
        constexpr std::uint32_t labels[]={0x1adb9u,0x1adb8u,0x1adbbu};
        Require(editing_.savedAudioMode>=0 && editing_.savedAudioMode<3,"Audio mode is outside its three source choices");
        state_->services.ui->resources->SetLanguageID(state_->screen,0x2932dfcau,labels[editing_.savedAudioMode]);
    }
    void Commit()
    {
        state_->services.frontend->musicSettings=editing_;
        auto& services=state_->services;
        Require(services.raceSetup && services.raceSetup->parameters && services.ui->sound,
            "Audio commit has no canonical race or speech-volume settings");
        services.frontend->ApplyAudioOptions(*services.sounds,*services.raceSetup->parameters,*services.ui->sound);
    }
    void HandleEvent(ScreenMessage message,ScreenCallbackData& data) override
    {
        auto& state=*state_;auto& services=state.services;auto& ui=*services.ui;
        const bool pointer=message==ScreenMessage::SliderDrag || message==ScreenMessage::SliderCommit;
        if(data.key==MenuMaster && master_) {
            if(pointer) {
                AdjustMaster(FlailerSlider::VolumeAtPointer(data.value),message==ScreenMessage::SliderDrag,true);
                return;
            }
            if(message==ScreenMessage::Highlight) {
                scrolling_.HandleEvent(ScreenMessage::Highlight,data);
                constexpr Translation help{"Set the overall game volume.","Regler le volume general du jeu.","Gesamtlautstaerke des Spiels einstellen.",
                    "Regola il volume generale del gioco.","Ajusta el volumen general del juego.","Stall in spelets huvudvolym."};
                blurb_.NewText(help[MenuLanguage(services)]);
                OptionsScreenBase::HandleHighlight(message,data,scrolling_.GetCurrentMenuMask());
                arrows_.SetCurrentItem(0);return;
            }
            if(message==ScreenMessage::Left || message==ScreenMessage::Right || message==ScreenMessage::Select) {
                const auto previous=services.pcHost->PcSettings()->masterVolume*0.01f;
                AdjustMaster(previous+(message==ScreenMessage::Left?-1.0f:1.0f)*FlailerSlider::VolumeStep,false,false);
                data.sound=0;return;
            }
        }
        if(pointer) {
            if(data.key>=MenuFirst63 && data.key<=MenuFirst63+6)
                sliders_[data.key-MenuFirst63]->SetDisplayValue(FlailerSlider::VolumeAtPointer(data.value),true,true);
            return;
        }
        scrolling_.HandleEvent(message,data);arrows_.HandleEvent(message,data);
        if(message==ScreenMessage::LeaveScreen) {
            EndMasterPreview();
            if(!SameAudioOptions63(editing_,services.frontend->musicSettings)) {
                Commit();if(services.frontend->autoSave)services.screens->stack.Push("Autosave");
            }
        } else if(message==ScreenMessage::ScreenBack) data.sound=0;
        else if(message==ScreenMessage::Previous && data.key==(services.pcHost?MenuMaster:MenuFirst63))data.value=1;
        else if(message==ScreenMessage::Next && data.key==0xaee77884u)data.value=1;
        else if(message==ScreenMessage::Highlight) {
            const auto index=data.key-MenuFirst63;
            if(index<std::size(AudioVolumeHints))blurb_.NewText(AudioVolumeHints[index][MenuLanguage(services)]);
            else if(index==7 || index==8)blurb_.NewText(0x24be4+static_cast<std::int32_t>(index));
            else if(data.key==0xaee77883u)blurb_.NewText(0x24bed);
            else if(data.key==0xaee77884u)blurb_.NewText(0x24bee);
            else blurb_.NewText("");
            OptionsScreenBase::HandleHighlight(message,data,scrolling_.GetCurrentMenuMask());
            arrows_.SetCurrentItem(data.key==0x52e039fau?0x2932dfcau:0);
        } else if(message==ScreenMessage::Left || message==ScreenMessage::Right) {
            if(data.key==0x52e039fau) {
                const auto current=worldAudioGetMode();
                Require(current>=0 && current<3,"Current source audio mode is outside its three choices");
                const auto requested=(current+(message==ScreenMessage::Left?2:1))%3;
                Require(worldAudioSetMode(services.sounds->audio,requested)==WORLD_AUDIO_OK,
                    "Audio mode change could not reach its canonical mixer");
                editing_.savedAudioMode=worldAudioGetMode();
                RedrawMode();data.sound=message==ScreenMessage::Left?1:2;
            } else if(data.key>=MenuFirst63 && data.key<=MenuFirst63+6) {
                auto& slider=*sliders_[data.key-MenuFirst63];
                slider.SetDisplayValue(slider.position.desiredValue+
                    (message==ScreenMessage::Left?-1.0f:1.0f)*FlailerSlider::VolumeStep,true);
            }
        } else if(message==ScreenMessage::Select) {
            if(data.key==0x52e039fbu) {
                Commit();state.exit=std::make_unique<FlailerExitScreenAnimation>(ui,*services.screens,state.screen,"JukeBox Music",true);
                float x{},y{};const auto* anchor=ui.resources->FindObject(state.screen,0x32c91daeu);
                Require(anchor,"Audio exit is missing its authored rail");ui.resources->GetTopLeft(*anchor,x,y,ui.language);
                state.exit->AddGroup(0x97bf4b25u,0x32c91daeu,x,650.0f,state.exit->MaximumDuration());
            } else if(data.key==0xaee77883u) {
                // PAL ResetVolumes1AFAB8 uses actual demo policy, then the
                // canonical defaults and saves the same live speech values.
                Require(ui.sound && ui.sound->volumes,"Volume reset has no actual default settings");
                const auto* demo=services.demoMode?services.demoMode->demoDisc:nullptr;
                const float scale=demo && demo->ActivationWord()?static_cast<float>(demo->Parameters().masterVolumeScale)*0.10000000149011612f:1.0f;
                auto& mixer=*services.sounds->audio->library;
                for(std::int32_t channel=0;channel<7;++channel)
                    Require(mixer.SetCategoryVolume(static_cast<std::uint32_t>(channel),
                        sound::GetVolumeDefault(*ui.sound->volumes,channel)*scale)==WORLD_AUDIO_OK,"Volume reset rejected by mixer");
                sound::SaveVolumesForSpeech(*ui.sound);
                for(std::size_t index=0;index<7;++index) {
                    editing_.savedVolumes[index]=mixer.GetCategoryVolume(AudioChannels63[index]);
                    sliders_[index]->ShowValue(sliders_[index]->ValueToDisplay(editing_.savedVolumes[index]));
                }
            } else if(data.key==0xaee77884u) {
                Require(editing_.count>=0 && static_cast<std::size_t>(editing_.count)<=editing_.rows.size(),"Music settings count is invalid");
                for(std::int32_t index=0;index<editing_.count;++index) {
                    auto& row=editing_.rows[static_cast<std::size_t>(index)];
                    row.status=static_cast<std::int8_t>(soundJukeboxGetTrackStatus(*services.sounds,row.group,row.track));
                }
                editing_.menuState={};editing_.menuMode=0;editing_.enabledGenres=255;
            } else data.sound=0;
        } else if(message==ScreenMessage::Tick) {
            if(master_)master_->Animate();
            for(auto& slider:sliders_)slider->Animate();
            blurb_.Animate();
        }
    }
    static void Callback(ScreenMessage message,ScreenCallbackData* data)
    {
        if(message==ScreenMessage::ScreenActivate) {
            Require(data,"Audio activation has no source screen data");
            Install(std::make_unique<AudioOptionsScreen>(BoundServices(),data->key));
        } else BaseCallback(message,data);
    }
};
} // namespace
} // namespace hp2::frontend

namespace hp2::frontend {
namespace {
class DisplayOptionsScreen final:public OptionsScreenBase {
    GlobalDisplaySettings editing_;
    FlailerScrollingMenuList scrolling_;
    FlailerLeftRightArrowScroll arrows_;
    FlailerAnimatedBlurb blurb_;
    static constexpr std::array<std::uint32_t,6> Values{{0x55cb09f2u,0x2e931f1bu,
        0x03d90172u,0x5610f2f4u,0x422313cbu,0x8fe4086bu}};
public:
    DisplayOptionsScreen(MainMenuScreenServices& services,std::uint32_t screen)
        :OptionsScreenBase(services,screen),editing_(services.frontend->displaySettings),
         scrolling_(*services.ui,screen,MenuFirst63,MenuFirst63+6,0xedf2e4eeu,0x20d73d01u,0xc67cfb4au),
         arrows_(*services.ui,screen,0xd1cda594u,0x9345e4a7u),
         blurb_(*services.ui,screen,0x75980cf0u,0x75980cf0u,0x70a404ecu)
    {
        for(std::size_t index=0;index<6;++index)Redraw(index);
        constexpr std::uint32_t objects[]={0x52e039f3u,0x52e039f4u,0x52e039f5u,
            0x52e039f6u,0x52e039f7u,0x52e039f8u,0x52e039f9u,0x52e039fau,0x52e039fbu,
            0x55cb09f2u,0x2e931f1bu,0x03d90172u,0x5610f2f4u,0x8fe4086bu,0x422313cbu};
        scrolling_.AddObjects(objects,std::size(objects));scrolling_.SetItemCount(9);
        // Final1F354C/1F3564 set the same real clipping polygon on both arrows.
        for(const auto key:{0xd1cda594u,0x9345e4a7u}) {
            auto* object=services.ui->resources->FindObject(screen,key);
            if(!object)continue;
            object->SetClipToPoly(0xc67cfb4au);
        }
    }
    void HandleHighlight(ScreenMessage,ScreenCallbackData&,std::uint32_t) override {}
    void Redraw(std::size_t index)
    {
        std::uint32_t language{};
        switch(index) {
        case 0: {
            // Actual final2BA1D0 dispatch, including the empty mode7 label.
            constexpr std::uint32_t labels[]={0x22345u,0x22346u,0x22347u,0x223a8u,0x22348u,
                0x22349u,0x22353u,0,0x223a9u,0x223aau};
            if(static_cast<std::uint32_t>(editing_.singleViewTrackMapMode)<std::size(labels))
                language=labels[editing_.singleViewTrackMapMode];
            break;
        }
        case 1:language=editing_.authoredOption0?0x22352u:0x22353u;break;
        case 2:language=editing_.languageDependentOption==0?0x2234au:
            editing_.languageDependentOption==1?0x2234bu:0;break;
        case 3: {
            constexpr std::uint32_t labels[]={0x22353u,0x18e71u,0x22352u};
            Require(editing_.leaderboardMode>=0 && editing_.leaderboardMode<3,"Leaderboard display mode is invalid");
            language=labels[editing_.leaderboardMode];break;
        }
        case 4:language=editing_.authoredOption6?0x22352u:0x22353u;break;
        case 5:language=editing_.authoredOption7?0x22352u:0x22353u;break;
        default:throw std::runtime_error("Invalid original Display option index");
        }
        state_->services.ui->resources->SetLanguageID(state_->screen,Values[index],language);
    }
    void HandleEvent(ScreenMessage message,ScreenCallbackData& data) override
    {
        auto& state=*state_;auto& services=state.services;auto& ui=*services.ui;
        scrolling_.HandleEvent(message,data);arrows_.HandleEvent(message,data);
        if(message==ScreenMessage::Left || message==ScreenMessage::Right) {
            if(data.key>=MenuFirst63 && data.key<MenuFirst63+6) {
                const auto index=data.key-MenuFirst63;
                const auto direction=message==ScreenMessage::Left?-1:1;
                if(index==0) {
                    Require(services.raceSetup && services.raceSetup->parameters && ui.flow,
                        "Track-map option has no canonical race and GameFlow context");
                    globalDisplaySettingsCycleTrackMapMode(&editing_,direction,services.raceSetup->parameters,
                        &ui.flow->currentState,&services.frontend->playerCount);
                    if(editing_.singleViewTrackMapMode==6)editing_.splitViewTrackMapMode=editing_.pursuitTrackMapMode=6;
                    else {
                        editing_.splitViewTrackMapMode=static_cast<std::int8_t>(splitPlayerTrackMapModes[0]);
                        editing_.pursuitTrackMapMode=static_cast<std::int8_t>(pursuitTrackMapModes[0]);
                    }
                } else if(index==1)editing_.authoredOption0=editing_.authoredOption0==0;
                else if(index==2)editing_.languageDependentOption=editing_.languageDependentOption==0;
                else if(index==3)globalDisplaySettingsCycleLeaderboardMode(&editing_,direction);
                else if(index==4)editing_.authoredOption6=editing_.authoredOption6==0;
                else editing_.authoredOption7=editing_.authoredOption7==0;
                Redraw(index);data.sound=index>=4?1:(direction<0?1:2);
            }
            state.selectionChange.ForceStart();
        } else if(message==ScreenMessage::Highlight) {
            constexpr std::int32_t blurbs[]={0x24cac,0x24cad,0x24cae,0x24caf,0x24cb1,0x24cb2,0x24cb3,0x24cb4,0x24cb5};
            const auto index=data.key-MenuFirst63;
            const auto value=index<Values.size()?Values[index]:0;
            if(index>=6)state.selectionChange.SetCurrentMenuItem(0);
            blurb_.NewText(index<std::size(blurbs)?blurbs[index]:0);
            state.selectionChange.SetCurrentMenuItem(value);arrows_.SetCurrentItem(value);
            OptionsScreenBase::HandleHighlight(message,data,scrolling_.GetCurrentMenuMask());
        } else if((message==ScreenMessage::Previous && data.key==MenuFirst63) ||
                  (message==ScreenMessage::Next && data.key==MenuFirst63+8))data.value=1;
        else if(message==ScreenMessage::Select) {
            const char* destination=nullptr;
            if(data.key==MenuFirst63+6)destination="Customize P1 HUD";
            else if(data.key==MenuFirst63+7)destination="Customize P2 HUD";
            else if(data.key==MenuFirst63+8)destination="Calibrate display offset";
            else data.sound=0;
            if(destination) {
                services.frontend->displaySettings=editing_;
                state.exit=std::make_unique<FlailerExitScreenAnimation>(ui,*services.screens,state.screen,destination,true);
                float x{},y{};const auto* anchor=ui.resources->FindObject(state.screen,0x32c91daeu);
                Require(anchor,"Display exit is missing its authored rail");ui.resources->GetTopLeft(*anchor,x,y,ui.language);
                state.exit->AddGroup(0x97bf4b25u,0x32c91daeu,x,650.0f,state.exit->MaximumDuration());arrows_.SetCurrentItem(0);
            }
        } else if(message==ScreenMessage::LeaveScreen) {
            services.frontend->displaySettings=editing_;
            if(gDisplayOptionsChangeManager.Different(*services.frontend) && services.frontend->autoSave)
                services.screens->stack.Push("Autosave");
        } else if(message==ScreenMessage::Tick)blurb_.Animate();
    }
    static void Callback(ScreenMessage message,ScreenCallbackData* data)
    {
        if(message==ScreenMessage::ScreenActivate) {
            Require(data,"Display activation has no source screen data");
            Install(std::make_unique<DisplayOptionsScreen>(BoundServices(),data->key));
        } else BaseCallback(message,data);
    }
};
// JukeBoxMusicScreen1F02D8..1F1280 uses the same Options base, live FRED,
// authored text objects and actual cJukebox player as the other screens.
class JukeBoxMusicScreen final:public OptionsScreenBase {
    FrontendMusicSettings editing_;
    GenreMgr genres_;
    std::int32_t previewTicks_{};
    bool previewPending_{},listFocus_{};
    std::int32_t topRow_{};
    static constexpr std::array<std::uint32_t,5> Titles{{0x64a819c7u,0x64a819c8u,0x64a819c9u,0x64a819cau,0x64a819cbu}};
    static constexpr std::array<std::uint32_t,5> Artists{{0x03b57047u,0x03b57048u,0x03b57049u,0x03b5704au,0x03b5704bu}};
    static constexpr std::array<std::uint32_t,5> Statuses{{0x89fb635fu,0x8c508c00u,0x8ea5b4a1u,0x90fadd42u,0x935005e3u}};
    static constexpr std::array<std::uint32_t,5> FirstMarkers{{0x0b825845u,0x0b825846u,0x0b825847u,0x0b825848u,0x0b825849u}};
    static constexpr std::array<std::uint32_t,5> SecondMarkers{{0xfc0279b8u,0xfc0279b9u,0xfc0279bau,0xfc0279bbu,0xfc0279bcu}};
    static constexpr std::array<std::uint32_t,6> StatusLabels{{0x1add9u,0x1addau,0x1adddu,0x1adbbu,0x1ade4u,0x1addcu}};
    static std::uint32_t PackedColor(const std::array<std::uint32_t,3>& rgb)
    {
        return 0x80000000u | static_cast<std::uint32_t>(rgb[0]) |
            (static_cast<std::uint32_t>(rgb[1])<<8) | (static_cast<std::uint32_t>(rgb[2])<<16);
    }
    const GenreMgr::Genre& CurrentGenre() const
    {
        Require(editing_.menuMode<genres_.count,"Saved music genre is absent from the actual cartridge");
        return genres_.genres[editing_.menuMode];
    }
    FrontendMusicSettings::Row& SelectedRow()
    {
        const auto& genre=CurrentGenre();const auto selected=editing_.menuState[0];
        Require(selected>=0 && selected<genre.count,"Music selection exceeds its actual genre rows");
        return editing_.rows[genre.rows[static_cast<std::size_t>(selected)]];
    }
    const FrontendMusicSettings::Row& FavouriteRow() const
    {
        const auto row=editing_.menuState[1];
        Require(row>=0 && static_cast<std::size_t>(row)<editing_.rows.size(),
            "Saved music footer row exceeds the original settings table");
        return editing_.rows[static_cast<std::size_t>(row)];
    }
    bool GenreEnabled(std::int32_t genre) const
    {
        Require(genre>=0 && genre<6,"Saved music genre bit is outside the original genre bank");
        return (editing_.enabledGenres&(std::uint32_t{1}<<genre))!=0;
    }
    std::pair<std::string,std::string> TrackText(const FrontendMusicSettings::Row& row) const
    {
        const auto& sound=*state_->services.sounds;
        const auto* name=soundJukeboxGetTrackName(sound,row.group,row.track);
        Require(name,"Music title is absent from the actual cartridge metadata");
        std::string title(name);
        const auto* artist=soundJukeboxGetTrackArtist(sound,row.group,row.track);
        Require(artist,"Music artist is absent from the actual cartridge metadata");
        return {std::move(title),artist};
    }
    float PreviewElapsed() const
    {
        const auto difference=static_cast<std::uint32_t>(state_->services.ui->clock->realTimer)-
            static_cast<std::uint32_t>(previewTicks_);
        std::int32_t signedDifference;std::memcpy(&signedDifference,&difference,sizeof(signedDifference));
        return static_cast<float>(signedDifference)*0.0002500000118743628f;
    }
public:
    JukeBoxMusicScreen(MainMenuScreenServices& services,std::uint32_t screen)
        :OptionsScreenBase(services,screen),editing_(services.frontend->musicSettings)
    {
        Require(services.sounds && services.sounds->audio && services.sounds->audio->library &&
            services.raceSetup && services.raceSetup->parameters && services.ui->clock &&
            soundJukeboxPlayer.audio==services.sounds->audio && soundJukeboxPlayer.IsCartridgeLoaded(),
            "Music screen has no actual cartridge, mixer, race settings or source clock");
        editing_.menuState[0]=0;
        genres_.Populate(*services.sounds,editing_);ShowPlaylist();
    }
    void ShowPlaylist()
    {
        auto& ui=*state_->services.ui;auto& resources=*ui.resources;const auto screen=state_->screen;
        const auto& genre=CurrentGenre();const auto selected=editing_.menuState[0];
        Require(selected>=0 && selected<genre.count,"Music selection is outside the displayed genre");
        while(selected>=topRow_+5 && topRow_+5<genre.count)++topRow_;
        while(selected<topRow_ && topRow_>=0)--topRow_;
        const bool enabled=GenreEnabled(editing_.menuMode);
        const auto visible=std::min<std::int32_t>(5,genre.count);
        for(std::size_t index=0;index<Titles.size();++index) {
            if(static_cast<std::int32_t>(index)>=visible) {
                for(const auto key:{Titles[index],Artists[index],Statuses[index],FirstMarkers[index],SecondMarkers[index]})
                    resources.SetObjectVisibility(screen,key,0);
                continue;
            }
            const auto number=topRow_+static_cast<std::int32_t>(index);
            Require(number>=0 && number<genre.count,"Music scroller exceeded its genre index bank");
            const auto& row=editing_.rows[genre.rows[static_cast<std::size_t>(number)]];
            const auto text=TrackText(row);
            resources.Printf(screen,Artists[index],"%s",text.second.c_str());
            resources.Printf(screen,Titles[index],"%s",text.first.c_str());
            const auto status=enabled?row.status:0;
            Require(status>=0 && static_cast<std::size_t>(status)<StatusLabels.size(),
                "Music status is outside its six source choices");
            resources.SetLanguageID(screen,Statuses[index],StatusLabels[static_cast<std::size_t>(status)]);
            const bool highlighted=listFocus_ && selected==number;
            resources.SetObjectVisibility(screen,FirstMarkers[index],highlighted);
            resources.SetObjectVisibility(screen,SecondMarkers[index],highlighted);
            const auto color=highlighted?PackedColor(ui.highlightedRgb):
                (!enabled?PackedColor(ui.inactiveRgb):0x00808080u);
            for(const auto key:{Titles[index],Artists[index],Statuses[index]}) {
                resources.SetTextColor(screen,key,color);resources.SetObjectVisibility(screen,key,1);
            }
        }
        resources.SetObjectVisibility(screen,0xedf2e4eeu,listFocus_ && topRow_!=0);
        resources.SetObjectVisibility(screen,0x20d73d01u,listFocus_ && genre.count>=5 && topRow_!=genre.count-5);
        const auto footer=TrackText(FavouriteRow());
        resources.Printf(screen,0x0b41999au,"%s (%s)",footer.first.c_str(),footer.second.c_str());
        resources.SetFittedText(screen,0x52e039f3u,ui.language,genre.name.data());
        resources.SetTextColor(screen,0x52e039f3u,listFocus_?0x00808080u:PackedColor(ui.highlightedRgb));
        resources.SetObjectVisibility(screen,0xd1cda594u,!listFocus_);
        resources.SetObjectVisibility(screen,0x9345e4a7u,!listFocus_);
    }
    void PlayCurrentSelectedSong()
    {
        const auto& row=SelectedRow();
        Require(soundJukeboxPlayer.Start(static_cast<std::uint32_t>(row.group),
            static_cast<std::uint32_t>(row.track),0)==WORLD_AUDIO_OK,
            "Music preview could not start its actual selected stream");
    }
    void HandleEvent(ScreenMessage message,ScreenCallbackData& data) override
    {
        auto& services=state_->services;
        switch(message) {
        case ScreenMessage::LeaveScreen: {
            if(!SameAudioOptions63(editing_,services.frontend->musicSettings) && services.frontend->autoSave)
                services.screens->stack.Push("Autosave");
            services.frontend->musicSettings=editing_;
            Require(soundJukeboxSetSelection(*services.sounds,*services.frontend,
                *services.raceSetup->parameters,5)==WORLD_AUDIO_OK,"Music selection could not apply its actual track choices");
            const auto& row=FavouriteRow();
            const auto result=GenreEnabled(row.genreBit) && row.status!=0 ?
                soundJukeboxPlayer.Start(static_cast<std::uint32_t>(row.group),static_cast<std::uint32_t>(row.track),1):
                soundJukeboxPlayer.Start(1);
            Require(result==WORLD_AUDIO_OK,"Music exit could not resume the actual playlist");
            break;
        }
        case ScreenMessage::ScreenEvent13:
            editing_.enabledGenres^=static_cast<std::uint8_t>(std::uint32_t{1}<<editing_.menuMode);
            if(!GenreEnabled(editing_.menuMode)){listFocus_=false;topRow_=0;editing_.menuState[0]=0;}
            data.sound=5;ShowPlaylist();break;
        case ScreenMessage::Left:case ScreenMessage::Right: {
            const std::int32_t delta=message==ScreenMessage::Left?-1:1;
            if(listFocus_) {
                auto& row=SelectedRow();const auto value=static_cast<std::int32_t>(row.status)+delta;
                row.status=static_cast<std::int8_t>(value<0?5:value>5?0:value);
            } else {
                const auto value=static_cast<std::int32_t>(editing_.menuMode)+delta;
                editing_.menuMode=static_cast<std::uint8_t>(value<0?genres_.count-1:value>=genres_.count?0:value);
                editing_.menuState[0]=0;
            }
            data.sound=message==ScreenMessage::Left?1:2;ShowPlaylist();break;
        }
        case ScreenMessage::Previous:case ScreenMessage::Next:
            if(listFocus_) {
                auto& selected=editing_.menuState[0];
                if(message==ScreenMessage::Previous && selected==0 && topRow_==0) {
                    listFocus_=false;
                    Require(soundJukeboxPlayer.Stop()==WORLD_AUDIO_OK,"Music genre return could not stop its preview");
                    ShowPlaylist();break;
                }
                const auto candidate=selected+(message==ScreenMessage::Previous?-1:1);
                if(candidate<0 || candidate>=CurrentGenre().count){data.sound=0;break;}
                selected=candidate;data.sound=message==ScreenMessage::Previous?3:4;
                previewTicks_=services.ui->clock->realTimer;previewPending_=true;ShowPlaylist();
            } else if(message==ScreenMessage::Previous)data.sound=0;
            else if(GenreEnabled(editing_.menuMode)) {
                listFocus_=true;ShowPlaylist();previewTicks_=services.ui->clock->realTimer;previewPending_=true;
            } else data.sound=6;
            break;
        case ScreenMessage::Tick:
            if(PreviewElapsed()>0.5f && previewPending_){previewPending_=false;PlayCurrentSelectedSong();}
            if(PreviewElapsed()>30.0f) {
                Require(soundJukeboxPlayer.Update()==WORLD_AUDIO_OK,"Music preview update failed");
                if(soundJukeboxPlayer.state!=2 && !soundJukeboxPlayer.IsFading())PlayCurrentSelectedSong();
            }
            break;
        case ScreenMessage::Select:
            if(!listFocus_ && GenreEnabled(editing_.menuMode))listFocus_=true;
            else if(listFocus_) {
                auto& row=SelectedRow();row.status=static_cast<std::int8_t>((row.status+1)%6);
            }
            data.sound=5;ShowPlaylist();break;
        case ScreenMessage::ItemEvent:case ScreenMessage::ScreenBack:data.sound=0;break;
        default:break;
        }
    }
    static void Callback(ScreenMessage message,ScreenCallbackData* data)
    {
        if(message==ScreenMessage::ScreenActivate) {
            Require(data,"Music activation has no original screen payload");
            Install(std::make_unique<JukeBoxMusicScreen>(BoundServices(),data->key));
        } else BaseCallback(message,data);
    }
};

constexpr std::int32_t AudioHelp63[]={800060,800061,800500,800063,0};
constexpr std::int32_t DisplayHelp63[]={800067,800068,800311,800069,0};
}
std::array<FlailerScreenConfig,3>& AudioDisplayScreenConfigurations()
{
    static constexpr std::int32_t musicHelp[]={802220,802221,802223,802224,0};
    static std::array<FlailerScreenConfig,3> configurations{{
        {"Audio Options",&AudioOptionsScreen::Callback,{{static_cast<std::int32_t>(0xe116761cu),0,0,0}},{{-1,-1,-1,-1}},1,AudioHelp63},
        {"Display Options",&DisplayOptionsScreen::Callback,{{static_cast<std::int32_t>(0x8bc68340u),0,0,0}},{{-1,-1,-1,-1}},1,DisplayHelp63},
        {"JukeBox Music",&JukeBoxMusicScreen::Callback,{{static_cast<std::int32_t>(0x95b2b627u),0,0,0}},{{-1,-1,-1,-1}},1,musicHelp}}};
    return configurations;
}
} // namespace hp2::frontend

namespace hp2::frontend {
namespace {
// PAL1F3F20..1F4A2C; the switch tables at2BA370/2BA390 put Lookback
// and CameraSpring before the gauges, independent of basic-block order.
class CustomizeHUDScreen final:public OptionsScreenBase {
    std::int32_t port_;
    PlayerSettings editing_;
    FlailerAnimatedBlurb blurb_;
    FlailerBitmapChunk* left_{};
    FlailerBitmapChunk* right_{};
    static constexpr std::array<std::uint32_t,7> Values{{0x2e018745u,0xffc3dcd2u,
        0x4ceb3ed2u,0x1054482bu,0xc7e2117eu,0x6caf6d10u,0x6bd89c11u}};
    static PlayerSettings& Settings(MainMenuScreenServices& services,std::int32_t port)
    {
        Require(services.frontend && port>=0 && port<2 && services.frontend->players[port],
            "HUD options have no actual selected player profile");
        return services.frontend->players[port]->settings;
    }
    void Redraw(std::size_t index)
    {
        std::uint32_t language{};
        switch(index) {
        case 0:language=editing_.lookback==0?0x22356u:editing_.lookback==1?0x22351u:0;break;
        case 1:language=editing_.cameraSpring==1?0x22353u:0x22352u;break;
        case 2:language=(editing_.hudFlags&PLAYER_HUD_GAUGES)?0x22352u:0x22353u;break;
        case 3:language=(editing_.hudFlags&PLAYER_HUD_RACE_POSITION)?0x22352u:0x22353u;break;
        case 4:language=(editing_.hudFlags&PLAYER_HUD_LAP_INFO)?0x22352u:0x22353u;break;
        case 5: {
            constexpr std::uint32_t labels[]={0x2234cu,0x2234du,0x2234eu,0x2234fu,0x22353u};
            if(static_cast<std::uint32_t>(editing_.splitTimeMode)<std::size(labels))
                language=labels[editing_.splitTimeMode];
            break;
        }
        case 6:language=editing_.scoring?0x22352u:0x22353u;break;
        default:throw std::runtime_error("Invalid original HUD option index");
        }
        state_->services.ui->resources->SetLanguageID(state_->screen,Values[index],language);
    }
    void DoHighlight(std::uint32_t key)
    {
        if(key)ArrowAligner(*state_->services.ui,state_->screen,0xd1cda594u,0x9345e4a7u,key);
        if(left_)left_->visible=key?1:0;
        if(right_)right_->visible=key?1:0;
    }
public:
    CustomizeHUDScreen(MainMenuScreenServices& services,std::uint32_t screen,std::int32_t port)
        :OptionsScreenBase(services,screen),port_(port),editing_(Settings(services,port)),
         blurb_(*services.ui,screen,0x75980cf0u,0x75980cf0u,0x70a404ecu)
    {
        for(std::size_t index=0;index<Values.size();++index)Redraw(index);
        left_=static_cast<FlailerBitmapChunk*>(services.ui->resources->FindObject(screen,0xd1cda594u,2));
        right_=static_cast<FlailerBitmapChunk*>(services.ui->resources->FindObject(screen,0x9345e4a7u,2));
    }
    ~CustomizeHUDScreen() override
    {
        // Source dtor commits the complete48-byte logical settings record;
        // the parent Display change manager controls the later autosave.
        Settings(state_->services,port_)=editing_;
    }
    void HandleEvent(ScreenMessage message,ScreenCallbackData& data) override
    {
        const auto index=data.key-MenuFirst63;
        if(message==ScreenMessage::Left || message==ScreenMessage::Right) {
            if(index<Values.size()) {
                switch(index) {
                case 0:playerSettingsToggleLookback(&editing_);break;
                case 1:playerSettingsToggleCameraSpring(&editing_);break;
                case 2:playerSettingsToggleGauges(&editing_);break;
                case 3:playerSettingsToggleRacePosition(&editing_);break;
                case 4:playerSettingsToggleLapInfo(&editing_);break;
                case 5:playerSettingsCycleSplitTime(&editing_,message==ScreenMessage::Left?-1:1);break;
                case 6:playerSettingsToggleScoring(&editing_);break;
                }
                Redraw(index);data.sound=message==ScreenMessage::Left?1:2;
            }
            state_->selectionChange.ForceStart();
        } else if(message==ScreenMessage::Highlight) {
            constexpr std::int32_t blurbs[]={0x24d74,0x24d75,0x24d76,0x24d77,0x24d78,0x24d79,0x24cb0};
            if(index<Values.size()) {
                DoHighlight(index<2?data.key:Values[index]);blurb_.NewText(blurbs[index]);
                state_->selectionChange.SetCurrentMenuItem(Values[index]);
            } else {
                blurb_.NewText(0);state_->selectionChange.SetCurrentMenuItem(0);
            }
        } else if(message==ScreenMessage::Tick) {
            blurb_.Animate();AnimateCursor(*state_->services.ui,left_,right_,std::int32_t(0));
        } else if(message==ScreenMessage::Select || message==ScreenMessage::ScreenBack)data.sound=0;
    }
    static void Callback(ScreenMessage message,ScreenCallbackData* data,std::int32_t port)
    {
        if(message==ScreenMessage::ScreenActivate) {
            Require(data,"HUD activation has no authored screen data");
            Install(std::make_unique<CustomizeHUDScreen>(BoundServices(),data->key,port));
        } else BaseCallback(message,data);
    }
    static void CallbackP1(ScreenMessage message,ScreenCallbackData* data){Callback(message,data,0);}
    static void CallbackP2(ScreenMessage message,ScreenCallbackData* data){Callback(message,data,1);}
};
constexpr std::int32_t CustomizeHUDHelp63[]={800067,800068,800312,0};
}
std::array<FlailerScreenConfig,2>& CustomizeHUDScreenConfigurations()
{
    static std::array<FlailerScreenConfig,2> configurations{{
        {"Customize P1 HUD",&CustomizeHUDScreen::CallbackP1,{{static_cast<std::int32_t>(0x28891a22u),0,0,0}},{{-1,-1,-1,-1}},1,CustomizeHUDHelp63},
        {"Customize P2 HUD",&CustomizeHUDScreen::CallbackP2,{{static_cast<std::int32_t>(0x28891a22u),0,0,0}},{{-1,-1,-1,-1}},1,CustomizeHUDHelp63}}};
    return configurations;
}
} // namespace hp2::frontend

#include "Ecstasy.hpp"
namespace hp2::frontend {
namespace {
std::array<std::int32_t,2> displayCalibrationDefaults{};
bool displayCalibrationDefaultsCaptured{};
std::uint32_t displayCalibrationScreen{};
std::uint32_t displayCalibrationOffsetScreen{};

MainMenuScreenServices& CalibrationServices()
{
    Require(optionsServices && optionsServices->frontend && optionsServices->screens &&
        optionsServices->ui && optionsServices->ui->clock && optionsServices->ui->resources &&
        optionsServices->ui->render && optionsServices->ui->language,
        "Display calibration lacks its actual frontend/render/language owners");
    return *optionsServices;
}

std::int32_t CalibrationAdd(std::int32_t value,std::int32_t increment)
{
    // Original ADDIU wraps a 32-bit field. Avoid native signed overflow.
    const auto bits=static_cast<std::uint32_t>(value)+static_cast<std::uint32_t>(increment);
    std::int32_t result;std::memcpy(&result,&bits,sizeof(result));return result;
}

void DrawCalibrationHelp(MainMenuScreenServices& services,std::uint32_t screen,std::uint32_t textId)
{
    auto& ui=*services.ui;
    const auto* text=ui.language->GetString(static_cast<std::int32_t>(textId));
    Require(text,"Display calibration has no authored localized instruction");
    ui.render->RenderTextIntoRectangle(*ui.resources,screen,0x6586d35fu,0xaf18eb98u,text,1.0f);
}

void DisplayCalibrateCallback(ScreenMessage message,ScreenCallbackData* data)
{
    // Complete PAL1F4A30..1F4AD8; this screen has no OptionsScreenBase owner.
    Require(data,"Display calibration callback has no original message payload");
    auto& services=CalibrationServices();data->sound=0;
    if(message==ScreenMessage::LeaveScreen)data->sound=6;
    else if(message==ScreenMessage::ScreenActivate)displayCalibrationScreen=data->key;
    else if(message==ScreenMessage::RenderModels)
        DrawCalibrationHelp(services,displayCalibrationScreen,0x222f4u);
}

void DisplayCalibrateOffsetCallback(ScreenMessage message,ScreenCallbackData* data)
{
    // Complete PAL1F4B20..1F4C98, including the final Commit on every message.
    Require(data,"Display offset callback has no original message payload");
    auto& services=CalibrationServices();data->sound=0;
    auto& setting=services.frontend->displayCalibration;
    switch(message) {
    case ScreenMessage::Left:
        setting[0]=CalibrationAdd(setting[0],-4);
        if(setting[0]<-650)setting[0]=-650;else data->sound=1;
        break;
    case ScreenMessage::Right:
        setting[0]=CalibrationAdd(setting[0],4);data->sound=1;break;
    case ScreenMessage::Previous:
        setting[1]=CalibrationAdd(setting[1],-1);
        if(setting[1]<-50)setting[1]=-50;else data->sound=1;
        break;
    case ScreenMessage::Next:
        setting[1]=CalibrationAdd(setting[1],1);data->sound=1;break;
    case ScreenMessage::LeaveScreen:data->sound=6;break;
    case ScreenMessage::ScreenActivate:displayCalibrationOffsetScreen=data->key;break;
    case ScreenMessage::Select:
        data->sound=5;
        Require(services.screens->MoveToPreviousScreen(1,false,0),
            "Display offset calibration has no preceding original screen");
        break;
    case ScreenMessage::ScreenEvent8:
        Require(displayCalibrationDefaultsCaptured,"Display calibration factory offsets were not captured");
        setting=displayCalibrationDefaults;data->sound=5;break;
    case ScreenMessage::RenderModels:
        DrawCalibrationHelp(services,displayCalibrationOffsetScreen,0x222f3u);break;
    default:break;
    }
    ApplyDisplayCalibration(*services.frontend,*services.ui->clock);
}
constexpr std::int32_t CalibrationHelp63[]={800313,0};
}

void InitializeDisplayCalibration(FrontendPlayerManager& frontend,const GameTime& clock)
{
    // Original tDisplayCalibrationData constructor1F4AD8 captures the process
    // defaults and reads the actual current display slot into the FRED record.
    if(!displayCalibrationDefaultsCaptured) {
        eGetScreenDisplayOffsets(clock,displayCalibrationDefaults[0],displayCalibrationDefaults[1]);
        displayCalibrationDefaultsCaptured=true;
    }
    eGetScreenDisplayOffsets(clock,frontend.displayCalibration[0],frontend.displayCalibration[1]);
}

void ApplyDisplayCalibration(const FrontendPlayerManager& frontend,const GameTime& clock)
{
    // Original tDisplayCalibrationData::Commit1F4B10 tail-calls eSet.
    eSetScreenDisplayOffsets(clock,frontend.displayCalibration[0],frontend.displayCalibration[1]);
}

std::array<FlailerScreenConfig,2>& DisplayCalibrationScreenConfigurations()
{
    static std::array<FlailerScreenConfig,2> configurations{{
        {"Calibrate Display",&DisplayCalibrateCallback,{{static_cast<std::int32_t>(0xddf09124u),0,0,0}},{{-1,-1,-1,-1}},1,CalibrationHelp63},
        {"Calibrate display offset",&DisplayCalibrateOffsetCallback,{{static_cast<std::int32_t>(0x7ab428c1u),0,0,0}},{{-1,-1,-1,-1}},1,CalibrationHelp63}}};
    return configurations;
}
} // namespace hp2::frontend

// Original Options.obj / indep/src/frontendscreens/options.cpp:
// CreditsScreen, CreditsMoviePlayer, CreditsLine, tCreditsSlideshow and
// CreditsScroller. Native services borrow the existing application owners.
#include "CarSounds.hpp"
#include "EventTree.hpp"
#include "Language.hpp"
#include "GameFlow.hpp"
#include "indep/src/MoviePlayer/MV3Codec.h"
#include "bware/inc/bWare.hpp"
#include <algorithm>
#include <array>
#include <cstdio>
#include <cstring>
#include <limits>
#include <string>
#include <vector>

namespace hp2::frontend {
namespace {
constexpr std::int32_t CreditsHelp[]={800080,800081,800079,0};
constexpr std::int32_t CreditsScrollHelp[]={800082,800083,800084,0};
constexpr const char* CreditsNames[]={"Credits","Credits FE Movie","Credits Reward movie",
    "BBG Creation movie","BBG Credits movie","EACredits","BBGCredits"};
constexpr std::uint32_t CreditsBackground=0x95ad096bu;
constexpr std::uint32_t CreditsFirstChoice=0x52e039f3u;
constexpr std::uint32_t CreditsHintText=0x75980cf0u;
constexpr struct {
    const char* destination;
    Translation hint;
} CreditsChoices[]={
    {"EACredits",{"Read the Electronic Arts credits.","Voir les credits Electronic Arts.","Die Mitwirkenden von Electronic Arts ansehen.",
        "Leggi i crediti Electronic Arts.","Consulta los creditos de Electronic Arts.","Las Electronic Arts eftertexter."}},
    {"BBG Credits movie",{"See the Black Box team credits.","Voir les credits de l'equipe Black Box.","Die Mitwirkenden von Black Box ansehen.",
        "Guarda i crediti del team Black Box.","Mira los creditos del equipo Black Box.","Se Black Box-teamets eftertexter."}},
    {"Credits FE Movie",{"Replay the game's opening movie.","Revoir le film d'introduction.","Das Intro des Spiels erneut ansehen.",
        "Rivedi il filmato introduttivo.","Vuelve a ver el video de apertura.","Spela upp spelets intro igen."}},
    {"BBG Creation movie",{"See how the game was made.","Voir les coulisses de la creation du jeu.","Einen Blick hinter die Kulissen werfen.",
        "Scopri come e stato creato il gioco.","Mira como se hizo el juego.","Se hur spelet skapades."}},
    {"Credits Reward movie",{"Watch the unlocked bonus movie.","Voir le film bonus debloque.","Das freigeschaltete Bonusvideo ansehen.",
        "Guarda il filmato bonus sbloccato.","Mira el video de bonificacion desbloqueado.","Se den upplasta bonusfilmen."}},
};
FrontendStartupServices* creditsServices{};

MainMenuScreenServices& CreditsMenus() {return *creditsServices->menus;}
void CreditsMusicStart(MainMenuScreenServices& menus)
{
    const auto& setup=*menus.raceSetup;
    Require(soundJukeboxStart(*menus.sounds,*menus.frontend,*setup.parameters,*setup.flow,true)==WORLD_AUDIO_OK,
        "Credits jukebox could not restart on its actual audio owner");
}
void CreditsGrayItem(MainMenuScreenServices& menus,std::uint32_t screen,std::uint32_t key,bool available)
{
    // The released GrayMenuItem(...,1/2,screen) enables/shows or disables/hides.
    auto* node=menus.screens->FindScreen(static_cast<std::int32_t>(screen));
    if (!node || !node->screen || !node->screen->menu)return;
    auto* item=node->screen->menu->FindItem(key);
    if (!item)return;
    if (available)item->SetMenuItemState(1);else item->ClearMenuItemState(1);
    menus.ui->resources->SetObjectVisibility(screen,key,available?1:0);
}
class CreditsScreen final {
public:
    CreditsScreen(MainMenuScreenServices& menus,std::uint32_t screen)
        : menus_(menus),screen_(screen),slide_(*menus.ui,screen,0xaed1a723u,0x718cd8c3u,CreditsBackground),
          highlight_(*menus.ui,screen,0xab671c9au,menus.ui->animationSettings.highlightColor)
    {
        menus_.screens->StartMovie("MOVIES\\options_movie_new.AVI");
        CreditsGrayItem(menus_,screen_,0x52e039f7u,ShouldShowRewardMovieOption());
        auto& resources=*menus_.ui->resources;
        const auto* source=static_cast<const FlailerTextChunk*>(resources.FindObject(CreditsBackground,CreditsHintText,1));
        const auto* first=static_cast<const FlailerTextChunk*>(resources.FindObject(screen_,CreditsFirstChoice,1));
        Require(source && first,"Credits hint has no frontend text template");
        auto& hint=resources.CloneNativeText(screen_,*source,CreditsHintText);
        hint.parentKey=0;
        hint.SetClipToPoly(0);
        hint.ClearFormatOptions(0x10);
        hint.authoredFlags=first->authoredFlags;
        hint.SetColor(first->color);
        hint.SetTopLeftX(191.0f);
        hint.SetTopLeftY(345.0f);
        hint.SetLanguageID(0);
        hint.SetString("");
        // Credits keeps Options behind its panel. Its hint is installed afresh
        // when Options is reactivated and highlights the selected row.
        resources.SetLocalizedText(CreditsBackground,CreditsHintText,"");
    }
    bool ShouldShowRewardMovieOption() const
    {
        // Final2026F8: live event32 in BOTH canonical progress trees must be
        // status4. The original GP cells map to PrimaryTree1 and PrimaryTree0.
        const auto* events=menus_.raceSetup->events;
        Require(events,"Credits reward query has no actual EventTree resource owner");
        auto* first=events->PrimaryTree(1);
        auto* second=events->PrimaryTree(0);
        Require(first && second,"Credits reward query lacks one original event tree");
        const auto* a=first->GetEventNumber(32);const auto* b=second->GetEventNumber(32);
        Require(a && b,"Credits reward query lacks authored event32");
        return a->GetStatus()==4 && b->GetStatus()==4;
    }
    void HandleEvent(ScreenMessage message,ScreenCallbackData& data)
    {
        highlight_.HandleEvents(message,data);
        if (message==ScreenMessage::Tick)slide_.Tick();
        const auto index=data.key-CreditsFirstChoice;
        if(index>=std::size(CreditsChoices))return;
        const auto& choice=CreditsChoices[index];
        if(message==ScreenMessage::Highlight)
            menus_.ui->resources->SetFittedText(screen_,CreditsHintText,menus_.ui->language,
                choice.hint[MenuLanguage(menus_)]);
        else if(message==ScreenMessage::Select)menus_.screens->ChangeToScreen(choice.destination,true);
    }
private:
    MainMenuScreenServices& menus_;
    std::uint32_t screen_;
    FlailerSlideOnScreen slide_;
    FlailerHighlightAnimation highlight_;
};

class CreditsMoviePlayer final {
public:
    CreditsMoviePlayer(FrontendStartupServices& services,std::uint32_t screen,
        const char* movie,const char* following)
        : services_(services),following_(following)
    {
        auto& menus=*services_.menus;auto& ui=*menus.ui;
        Require(movie && ui.movie && services_.movieLog,"Credits movie has no actual file/player/replay owners");
        const bool creation=std::strcmp(movie,"MOVIES\\INTRO\\MAKEOF_EN.AVI")==0;
        const auto key=creation?0x3acf2d73u:0xbfaebf79u;
        bitmap_=static_cast<FlailerBitmapChunk*>(ui.resources->FindObject(screen,key,2));
        Require(bitmap_,"Credits movie lacks its authored movie bitmap");
        ui.resources->SetObjectVisibility(screen,0x3acf2d73u,creation?1:0);
        ui.resources->SetObjectVisibility(screen,0xbfaebf79u,creation?0:1);
        Require(soundJukeboxStop(*menus.sounds)==WORLD_AUDIO_OK,
            "Credits movie could not stop the actual jukebox");
        menus.screens->StopMovie();
        try {
            Require(ui.movie->InitFile(movie)==1,"Credits movie could not open its original authored AVI");
            ui.resources->SetObjectVisibility(screen,0xb070dbc9u,0);
            ui.resources->SetGroupVisibility(screen,0xb73c8d23u,0);
            bitmap_->SetNativeFullscreenMovie(true);
        } catch (...) {
            const auto primary=std::current_exception();
            if (const auto cleanup=ui.movie->Shutdown()) {
                try {std::rethrow_exception(cleanup);}
                catch (const std::exception& error) {platform::RecordSessionEvent("movie.credits.cleanup_failed",error.what());}
                catch (...) {platform::RecordSessionEvent("movie.credits.cleanup_failed","Unknown movie cleanup failure");}
            }
            try {CreditsMusicStart(menus);}
            catch (const std::exception& error) {platform::RecordSessionEvent("movie.credits.music_restore_failed",error.what());}
            catch (...) {platform::RecordSessionEvent("movie.credits.music_restore_failed","Unknown credits music restore failure");}
            std::rethrow_exception(primary);
        }
    }
    void Retire()
    {
        // PAL1F2358 destructor actions. Explicit retirement lets a failed
        // real audio submission preserve this owner for the caller's retry;
        // unique_ptr destruction is never a throwing acknowledgement path.
        if (retired_)return;
        services_.menus->ui->movie->Close();
        bitmap_->SetNativeFullscreenMovie(false);
        // Ordinary transitions restart music; terminal retirement has closed
        // admission and must not create another physical audio borrower.
        if (!carLoader.ShuttingDown())CreditsMusicStart(*services_.menus);
        retired_=true;
    }
    void MovieFinished()
    {
        auto& screens=*services_.menus->screens;
        if (following_)screens.ChangeToScreen(following_,false);
        else screens.MoveToPreviousScreen(1,false,0);
    }
    void HandleEvent(ScreenMessage message,ScreenCallbackData& data)
    {
        if (message==ScreenMessage::Tick) {
            if (DriveMovie(*services_.menus->ui,*bitmap_,*services_.movieLog)==1) {
                services_.menus->ui->movie->Close();MovieFinished();
            }
        } else if (message==ScreenMessage::Select)MovieFinished();
        else if (message==ScreenMessage::Left || message==ScreenMessage::Right ||
            message==ScreenMessage::Previous || message==ScreenMessage::Next || message==ScreenMessage::ScreenBack)
            data.sound=0;
    }
private:
    FrontendStartupServices& services_;
    const char* following_{};
    FlailerBitmapChunk* bitmap_{};
    bool retired_{};
};

bool CreditsDigit(char c) {return c>='0' && c<='9';}
bool CreditsLetter(char c) {return (c>='a' && c<='z') || (c>='A' && c<='Z');}
const char* CreditsSkipToDigit(const char* text) {while (*text && !CreditsDigit(*text))++text;return text;}
const char* CreditsSkipToLetter(const char* text) {while (*text && !CreditsLetter(*text))++text;return text;}
std::uint32_t CreditsDecimal(const char* text)
{
    std::uint32_t value=0;
    while (CreditsDigit(*text)) {value=value*10u+static_cast<std::uint32_t>(*text-'0');++text;}
    return value;
}
std::uint32_t ParseColor(const char* text)
{
    std::uint32_t channels[]={128,128,128};
    const char* cursor=CreditsSkipToDigit(text);
    for (std::size_t i=0;i<3 && *cursor;++i) {
        channels[i]=CreditsDecimal(cursor);
        const char delimiter=i==2?')':',';
        const auto* end=std::strchr(cursor,delimiter);
        Require(end,"Credits RGB directive has no authored closing delimiter");
        cursor=CreditsSkipToDigit(end);
    }
    return 0x80000000u|channels[0]|(channels[1]<<8)|(channels[2]<<16);
}
std::uint32_t ParseFont(const char* text)
{
    const auto* first=CreditsSkipToLetter(text);
    if (!*first)return 0;
    const auto* end=std::strchr(first,')');
    Require(end,"Credits FONT directive has no authored closing delimiter");
    const std::string font(first,static_cast<std::size_t>(end-first));
    return hashString(font.c_str());
}
void ParseCreditsColors(std::uint32_t& color,std::uint32_t& font,char* output,
    std::size_t capacity,const char* source)
{
    Require(output && capacity && source,"Credits line parser has no actual text storage");
    std::array<char,32> fontText{},colorText{};
    std::fill_n(output,capacity,char{});
    bool command=false,parenthesis=false,fontCommand=false,colorCommand=false;
    char* token=nullptr;std::size_t tokenSize=0,outputSize=0;
    for (const char* current=source;*current;++current) {
        if (*current=='$') {
            if (current[1]=='$')++current;
            else {
                if (std::strncmp(current+1,"FONT",4)==0)fontCommand=true;
                if (std::strncmp(current+1,"RGB",3)==0)colorCommand=true;
                if (fontCommand || colorCommand)command=true;
            }
        }
        if (*current=='(' && command)parenthesis=true;
        if (!command && !parenthesis) {
            Require(outputSize+1<capacity,"Credits text exceeds the original bounded line buffer");
            output[outputSize++]=*current;
        }
        if (parenthesis && !token) {
            if (fontCommand)token=fontText.data();
            else if (colorCommand)token=colorText.data();
            tokenSize=0;
        }
        if (token) {
            Require(tokenSize+1<fontText.size(),"Credits directive exceeds its original bounded token buffer");
            token[tokenSize++]=*current;token[tokenSize]=0;
        }
        if (*current==')') {
            parenthesis=false;command=false;fontCommand=false;colorCommand=false;token=nullptr;
        }
    }
    Require(!command && !parenthesis,"Credits directive is truncated");
    if (colorText[0])color=ParseColor(colorText.data());
    if (fontText[0])font=ParseFont(fontText.data());
}

class CreditsLine final {
public:
    CreditsLine(FlailerUiState& ui,FlailerTextChunk& text):ui_(ui),text_(text) {}
    float SetLanguageID(std::int32_t id,float y)
    {
        std::array<char,256> display{};std::uint32_t color=0,font=0;
        const auto* localized=ui_.language->GetString(id);
        Require(localized,"Credits line has no authored translation");
        ParseCreditsColors(color,font,display.data(),display.size(),localized);
        text_.SetColor(color?color:0x80808080u);
        if (font==hashString("FONT_IMPACTBIG") || font==hashString("FONT_IMPACTNORMAL")) {
            text_.SetScaleX(0.6000000238418579f);text_.SetScaleY(0.6000000238418579f);text_.fontKey=font;
        } else {
            text_.SetScaleX(1.0f);text_.SetScaleY(1.0f);text_.fontKey=hashString("FONT_STATS");
        }
        text_.updateKind=1;
        if (color || font)text_.SetString(display.data());else text_.SetLanguageID(static_cast<std::uint32_t>(id));
        text_.SetTopLeftY(y);text_.SetClipToPoly(0xa94bd818u);
        return ui_.resources->ScaledTextHeight(text_);
    }
    void Animate(float elapsed)
    {
        float x,y;ui_.resources->GetTopLeft(text_,x,y,ui_.language);text_.SetTopLeftY(y-elapsed*30.0f);
    }
    bool PleaseResetMe() const
    {
        float x,y;ui_.resources->GetTopLeft(text_,x,y,ui_.language);return y<0.0f;
    }
    float Bottom() const
    {
        float x,y;ui_.resources->GetTopLeft(text_,x,y,ui_.language);return y+ui_.resources->ScaledTextHeight(text_);
    }
private:
    FlailerUiState& ui_;
    FlailerTextChunk& text_;
};
class tCreditsSlideshow final {
public:
    tCreditsSlideshow(FlailerUiState& ui,std::uint32_t screen,std::uint32_t first,std::uint32_t second)
        : ui_(ui),screen_(screen),first_(first),second_(second),started_(ui.clock->realTimer),
          current_(hashString("CREDITS_BG1")),secondAlpha_(ui,screen),firstAlpha_(ui,screen)
    {
        secondAlpha_.AddObject(second);firstAlpha_.AddObject(first);
        if (auto* object=ui_.resources->FindObject(screen_,second_))object->SetAlpha(0);
    }
    std::uint32_t PickNewTexture(std::uint32_t previous)
    {
        for (std::int32_t attempt=0;attempt<10;++attempt) {
            char name[32];std::snprintf(name,sizeof(name),"CREDITS_BG%u",vectorDefaultRandomBounded(8)+1);
            const auto key=hashString(name);if (key!=previous)return key;
        }
        return previous;
    }
    void Animate()
    {
        const auto raw=static_cast<std::uint32_t>(ui_.clock->realTimer)-static_cast<std::uint32_t>(started_);
        std::int32_t delta;std::memcpy(&delta,&raw,sizeof(delta));
        float elapsed=static_cast<float>(delta)*0.0002500000118743628f;
        if (choose_) {current_=PickNewTexture(current_);choose_=false;}
        if (!(elapsed>4.0f))return;
        if (elapsed>5.0f) {
            ChangeTexture(first_,current_);started_=ui_.clock->realTimer;choose_=true;
        } else {
            elapsed-=4.0f;ChangeTexture(second_,current_);
            secondAlpha_.SetAlphaScale(elapsed);firstAlpha_.SetAlphaScale(1.0f-elapsed);
        }
    }
private:
    void ChangeTexture(std::uint32_t object,std::uint32_t texture)
    {
        if (auto* target=ui_.resources->FindObject(screen_,object,2))
            static_cast<FlailerBitmapChunk*>(target)->ChangeTexture(texture);
    }
    FlailerUiState& ui_;
    std::uint32_t screen_,first_,second_;
    std::int32_t started_;
    std::uint32_t current_;
    bool choose_{true};
    FlailerAlphaAnimation secondAlpha_,firstAlpha_;
};
class CreditsScroller final {
public:
    CreditsScroller(MainMenuScreenServices& menus,std::uint32_t screen)
        : menus_(menus),screen_(screen),slideshow_(*menus.ui,screen,0xf947e681u,0x4ef4d164u)
    {
        Require(menus_.screens->active && menus_.screens->active->configuration,
            "Credits scroll has no actual active configuration");
        const auto* name=menus_.screens->active->configuration->name;
        BuildCredits(std::strcmp(name,"EACredits")==0?0:1);
        menus_.screens->StopMovie();
    }
    void BuildCredits(std::int32_t kind)
    {
        Require(kind==0 || kind==1,"Credits scroll has no authored credit range");
        first_=kind==0?501000:601000;last_=kind==0?501614:601226;next_=first_;
        auto& ui=*menus_.ui;
        auto* object=ui.resources->FindObjectByGroup(screen_,0xd2638b19u,1);
        while (object) {
            Require(object->kind==1,"Credits text group contains a non-text object");
            lines_.emplace_back(ui,static_cast<FlailerTextChunk&>(*object));
            object=ui.resources->FindObjectByGroup(screen_,0xd2638b19u,0,object);
        }
        Require(!lines_.empty(),"Credits screen has no authored line pool");
        float y=240.0f;
        for (auto& line:lines_)y+=line.SetLanguageID(next_++,y);
    }
    float GetMaxY() const
    {
        float result=-10.0f;for (const auto& line:lines_)if (result<line.Bottom())result=line.Bottom();return result;
    }
    void Tock(float elapsed)
    {
        for (auto& line:lines_) {
            if (line.PleaseResetMe()) {
                const auto id=next_++;line.SetLanguageID(id,GetMaxY());if (next_>last_)next_=first_;
            } else line.Animate(elapsed);
        }
    }
    void HandleEvent(ScreenMessage message,ScreenCallbackData& data)
    {
        if (message==ScreenMessage::Tick) {if (!paused_)Tock(menus_.ui->clock->realTimeElapsed);slideshow_.Animate();}
        else if (message==ScreenMessage::Left || message==ScreenMessage::Right ||
            message==ScreenMessage::Previous || message==ScreenMessage::Next) {data.sound=28;Tock(1.0f);}
        else if (message==ScreenMessage::ItemEvent) {data.sound=5;paused_=!paused_;}
        else if (message==ScreenMessage::Select)data.sound=0;
    }
private:
    MainMenuScreenServices& menus_;
    std::uint32_t screen_;
    std::vector<CreditsLine> lines_;
    std::int32_t first_{},last_{},next_{};
    bool paused_{};
    tCreditsSlideshow slideshow_;
};
std::unique_ptr<CreditsScreen> creditsOwner;
std::unique_ptr<CreditsMoviePlayer> creditsMovieOwner;
std::unique_ptr<CreditsScroller> creditsScrollOwner;
}

void CreditsScreenCallback(ScreenMessage message,ScreenCallbackData* data)
{
    Require(creditsServices && data,"Credits callback has no actual bindings/data");
    if (message==ScreenMessage::ScreenActivate) {
        Require(!creditsOwner,"Credits received duplicate activation");
        creditsOwner=std::make_unique<CreditsScreen>(CreditsMenus(),data->key);
    } else if (message==ScreenMessage::ScreenDeactivate)creditsOwner.reset();
    else {Require(creditsOwner!=nullptr,"Credits callback precedes activation");creditsOwner->HandleEvent(message,*data);}
}
void CreditsMoviePlayerCallback(ScreenMessage message,ScreenCallbackData* data)
{
    Require(creditsServices && data,"Credits movie callback has no actual bindings/data");
    if (message==ScreenMessage::ScreenActivate) {
        Require(!creditsMovieOwner,"Credits movie received duplicate activation");
        const auto& menus=CreditsMenus();
        Require(menus.screens->active && menus.screens->active->configuration,
            "Credits movie has no actual active configuration");
        const auto* name=menus.screens->active->configuration->name;
        const bool french=menus.ui->language->currentLanguage==1;
        const char* movie=nullptr;const char* following=nullptr;
        if (std::strcmp(name,"Credits FE Movie")==0)
            movie=french?"MOVIES\\INTRO\\NFSINTRO_FR.AVI":"MOVIES\\INTRO\\NFSINTRO_EN.AVI";
        else if (std::strcmp(name,"Credits Reward movie")==0)
            movie=french?"MOVIES\\INTRO\\REWARD_FR.AVI":"MOVIES\\INTRO\\REWARD_EN.AVI";
        else if (std::strcmp(name,"BBG Creation movie")==0)movie="MOVIES\\INTRO\\MAKEOF_EN.AVI";
        else if (std::strcmp(name,"BBG Credits movie")==0) {
            movie="MOVIES\\INTRO\\BBGLOGO_EN.AVI";following="BBGCredits";
        }
        Require(movie,"Credits movie has no released configuration mapping");
        creditsMovieOwner=std::make_unique<CreditsMoviePlayer>(*creditsServices,data->key,movie,following);
    } else if (message==ScreenMessage::ScreenDeactivate) {
        if (creditsMovieOwner)creditsMovieOwner->Retire();
        creditsMovieOwner.reset();
    } else {Require(creditsMovieOwner!=nullptr,"Credits movie callback precedes activation");creditsMovieOwner->HandleEvent(message,*data);}
}
void CreditsScrollerCallback(ScreenMessage message,ScreenCallbackData* data)
{
    Require(creditsServices && data,"Credits scroll callback has no actual bindings/data");
    if (message==ScreenMessage::ScreenActivate) {
        Require(!creditsScrollOwner,"Credits scroll received duplicate activation");
        creditsScrollOwner=std::make_unique<CreditsScroller>(CreditsMenus(),data->key);
    } else if (message==ScreenMessage::ScreenDeactivate)creditsScrollOwner.reset();
    else {Require(creditsScrollOwner!=nullptr,"Credits scroll callback precedes activation");creditsScrollOwner->HandleEvent(message,*data);}
}
void BindCreditsScreens(FrontendStartupServices& services)
{
    Require(!creditsOwner && !creditsMovieOwner && !creditsScrollOwner && services.menus && services.movieLog,
        "Credits binding lacks the actual frontend/movie replay owner");
    auto& menus=*services.menus;
    // Menu registration precedes the asynchronous FRONTB event publication.
    // Keep the live setup borrow here; ShouldShowRewardMovieOption validates
    // its actual event owner, both trees and event32 when Credits activates.
    Require(menus.ui && menus.ui->resources && menus.ui->language && menus.ui->clock && menus.ui->movie &&
        menus.ui->render && menus.screens && menus.screens->ui==menus.ui && menus.frontend && menus.sounds &&
        menus.raceSetup && menus.raceSetup->parameters && menus.raceSetup->flow,
        "Credits bindings do not share the actual application owners");
    creditsServices=&services;
}
void UnbindCreditsScreens()
{
    std::exception_ptr failure;
    try {if (creditsMovieOwner)creditsMovieOwner->Retire();}
    catch (...) {failure=std::current_exception();}
    // Do not retain static Flailer borrowers after one audio failure. Movie
    // retains its first error and pending voice for application shutdown.
    creditsMovieOwner.reset();creditsScrollOwner.reset();creditsOwner.reset();creditsServices=nullptr;
    if (failure)std::rethrow_exception(failure);
}
std::array<FlailerScreenConfig,7>& CreditsScreenConfigurations()
{
    static std::array<FlailerScreenConfig,7> configurations{{
        {CreditsNames[0],CreditsScreenCallback,{{static_cast<std::int32_t>(0xfd8a3b6du),0,0,0}},{{-1,-1,-1,-1}},1,CreditsHelp},
        {CreditsNames[1],CreditsMoviePlayerCallback,{{static_cast<std::int32_t>(0xb5761d00u),0,0,0}},{{-1,-1,-1,-1}},1,nullptr},
        {CreditsNames[2],CreditsMoviePlayerCallback,{{static_cast<std::int32_t>(0xb5761d00u),0,0,0}},{{-1,-1,-1,-1}},1,nullptr},
        {CreditsNames[3],CreditsMoviePlayerCallback,{{static_cast<std::int32_t>(0xb5761d00u),0,0,0}},{{-1,-1,-1,-1}},1,nullptr},
        {CreditsNames[4],CreditsMoviePlayerCallback,{{static_cast<std::int32_t>(0xb5761d00u),0,0,0}},{{-1,-1,-1,-1}},1,nullptr},
        {CreditsNames[5],CreditsScrollerCallback,{{static_cast<std::int32_t>(0x39c4c62cu),0,0,0}},{{-1,-1,-1,-1}},1,CreditsScrollHelp},
        {CreditsNames[6],CreditsScrollerCallback,{{static_cast<std::int32_t>(0x39c4c62cu),0,0,0}},{{-1,-1,-1,-1}},1,CreditsScrollHelp}
    }};
    return configurations;
}
}
