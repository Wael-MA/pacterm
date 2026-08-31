// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Wael (https://wael.work.gd)
// pacterm v1.4.0
#include "I18n.hpp"
#include <unordered_map>
#include <cstdlib>
#include <string_view>
#include <algorithm>
#include <cctype>

namespace I18n {

    namespace {

        const std::vector<LanguageInfo> s_languages = {
            {Language::En, "en", "English"}, {Language::Ar, "ar", "Arabiya"},  {Language::Fr, "fr", "Français"}, {Language::Es, "es", "Español"},
            {Language::De, "de", "Deutsch"}, {Language::It, "it", "Italiano"}, {Language::Ja, "ja", "日本語"},
        };

        Language s_current_language = Language::En;

        const std::unordered_map<std::string_view, std::string_view> kDictEn = {
            {"main_menu.start", "Start Game"},
            {"menu.start_game", "Start Game"},
            {"main_menu.username", "Username"},
            {"menu.username", "Username:       {}"},
            {"menu.username_short", "User: {}"},
            {"main_menu.stats", "Stats"},
            {"menu.stats", "Stats"},
            {"main_menu.redeem", "Redeem Code"},
            {"menu.redeem_code", "Redeem Code"},
            {"main_menu.install", "Install pacterm locally"},
            {"menu.install", "Install pacterm"},
            {"menu.install_local", "Install pacterm locally"},
            {"main_menu.uninstall", "Uninstall pacterm"},
            {"menu.uninstall", "Uninstall pacterm"},
            {"menu.uninstall_local", "Uninstall pacterm"},
            {"main_menu.settings", "Settings"},
            {"menu.settings", "Settings"},
            {"main_menu.quit", "Quit"},
            {"menu.exit", "Quit"},

            {"settings.title", "SETTINGS"},
            {"settings.general_theme", "General Theme"},
            {"settings.language", "Language"},
            {"settings.nerd_fonts", "Nerd Fonts"},
            {"settings.sound", "Sound"},
            {"settings.pacman_theme", "Pac-Man Theme"},
            {"settings.key_config", "Configure Keys"},
            {"settings.reset", "Reset"},
            {"settings.back", "Back to Menu"},
            {"settings.on", "ON"},
            {"settings.off", "OFF"},
            {"settings.reset_success", "Everything reset successfully!"},

            {"pause.title", "GAME PAUSED"},
            {"pause.resume", "Resume Game"},
            {"pause.theme_info", "Theme Info & Guide"},
            {"pause.restart", "Restart Level"},
            {"pause.return_menu", "Return to Menu"},
            {"pause.quit", "Quit Game"},

            {"hud.shield", "SHIELD ACTIVE"},
            {"hud.double_bounty", "2x BOUNTY: {}s"},
            {"hud.bounty", "2x BOUNTY: {}s"},
            {"hud.fever", "FEVER x2: {}s"},
            {"hud.freeze", "FREEZE: {}s"},
            {"hud.score", " SCORE: {}"},
            {"hud.score_anon", " SCORE: {}"},
            {"hud.high", "HIGH: {} "},
            {"hud.level", "LEVEL {} "},
            {"hud.lives", " LIVES: "},
            {"hud.muted", " [MUTED]"},
            {"hud.x2_score", "x2 SCORE: {}s"},
            {"hud.speed", "SPEED: {}s"},
            {"hud.magnet", "MAGNET: {}s"},

            {"game.ready", "READY!"},
            {"game.game_over", "GAME OVER"},
            {"game.press_continue", "Press ENTER to return to Menu"},
            {"game.press_any_key", "Press any key to return to the game"},

            {"stats.title", "STATS"},
            {"stats.high_score", "High Score"},
            {"stats.games_played", "Games Played"},
            {"stats.ghosts_eaten", "Ghosts Eaten"},
            {"stats.deaths", "Deaths"},
            {"stats.best_level", "Best Level"},
            {"stats.dots_eaten", "Dots Eaten"},
            {"stats.power_pellets", "Power Pellets"},
            {"stats.time_played", "Time Played"},
            {"stats.letters", "Letters"},
            {"stats.letter", "Letters"},

            {"key_config.title", "KEY CONFIGURATION"},
            {"key_config.up", "UP:    [ {} ]"},
            {"key_config.down", "DOWN:  [ {} ]"},
            {"key_config.left", "LEFT:  [ {} ]"},
            {"key_config.right", "RIGHT: [ {} ]"},
            {"key_config.pause", "PAUSE: [ {} ]"},
            {"key_config.press_key", "PRESS KEY..."},
            {"key_config.save_back", "Save & Back"},
            {"key_config.binding_prompt", "PRESS ANY KEY NOW (ESC TO CANCEL)"},
            {"key_config.enter_prompt", "Press ENTER to edit"},

            {"level_select.title", "SELECT LEVEL"},
            {"level_select.back", "BACK TO MENU"},

            {"theme_info.title", "THEME INFO"},
            {"theme_info.active_mechanics", "Active Mechanics:"},
            {"theme_info.theme", "Theme: "},
            {"theme_info.hazard_prefix", "• Hazard: "},
            {"theme_info.t0_m1", "Standard arcade pacing and balanced ghost AI"},
            {"theme_info.t0_m2", "Continuous dot chain multiplier scoring"},
            {"theme_info.t0_hz", "Tight corridor choke points"},
            {"theme_info.t1_m1", "Speed Surge: Pac-Man moves 20% faster"},
            {"theme_info.t1_m2", "Acid Trails: Toxic floor residue in wake"},
            {"theme_info.t1_hz", "Tight corridor choke points"},
            {"theme_info.t2_m1", "Warp Stun: Side tunnel warps stun ghosts"},
            {"theme_info.t2_m2", "Dot Bounty: Extra points for dot streaks"},
            {"theme_info.t2_hz", "High-speed ghost tunnel tracking"},
            {"theme_info.t3_m1", "Ember Shield: Thermal floor immunity"},
            {"theme_info.t3_m2", "Magma Multiplier: Double points on lava"},
            {"theme_info.t3_hz", "Lava Tiles: Floor tiles ignite periodically"},
            {"theme_info.t4_m1", "Dash Ability: Press [SPACE] to burst"},
            {"theme_info.t4_m2", "Fury Combos: Rapid ghost chains score 4x"},
            {"theme_info.t4_hz", "Aggressive pursuit algorithms"},
            {"theme_info.t5_m1", "Blitz Bounty: Rapid pellet eating frenzy"},
            {"theme_info.t5_m2", "Arcane Harvest: Distant dots pull to player"},
            {"theme_info.t5_hz", "Unstable teleportation spikes"},
            {"theme_info.t6_m1", "Permafrost: Ghost freeze extended"},
            {"theme_info.t6_m2", "Chilled Aura: Ghosts move slower near Pac-Man"},
            {"theme_info.t6_hz", "Icy Friction: Inertial turning slide"},
            {"theme_info.t7_m1", "Molten Shield: Invulnerability on fruits"},
            {"theme_info.t7_m2", "Golden Multiplier: 4x points on ghosts"},
            {"theme_info.t7_hz", "Persistent double-speed ghost frenzy"},
            {"theme_info.t8_m1", "Prismatic Spectrum: Dynamic multi-hue frenzy"},
            {"theme_info.t8_m2", "Continuous dot chain multiplier scoring"},
            {"theme_info.t8_hz", "Chromatic ghost acceleration"},
            {"theme_info.t9_m1", "Chaos Luck: Randomized surprise powerups"},
            {"theme_info.t9_m2", "Binary Warp: Unpredictable tunnel warps"},
            {"theme_info.t9_hz", "Flickering wall corruption noise"},
            {"theme_info.t10_m1", "Composite Master Theme: Combines all buffs"},
            {"theme_info.t10_m2", "Prismatic Scoring: Permanent 1.5x bonus"},
            {"theme_info.t10_hz", "Apex master ghost difficulty curve"},

            {"redeem.title", "REDEEM CODE"},
            {"redeem.hint", "ESC: Cancel  ENTER: Redeem"},

            {"username.title", "USERNAME"},
            {"username.hint", "ESC: Cancel  ENTER: Set"},

            {"common.on", "ON"},
            {"common.off", "OFF"},
        };

        const std::unordered_map<std::string_view, std::string_view> kDictFr = {
            {"main_menu.start", "Lancer la partie"},
            {"menu.start_game", "Lancer la partie"},
            {"main_menu.username", "Pseudo"},
            {"menu.username", "Pseudo:         {}"},
            {"menu.username_short", "Pseudo: {}"},
            {"main_menu.stats", "Statistiques"},
            {"menu.stats", "Statistiques"},
            {"main_menu.redeem", "Code cadeau"},
            {"menu.redeem_code", "Code cadeau"},
            {"main_menu.install", "Installer pacterm"},
            {"menu.install", "Installer pacterm"},
            {"menu.install_local", "Installer pacterm localement"},
            {"main_menu.uninstall", "Désinstaller pacterm"},
            {"menu.uninstall", "Désinstaller pacterm"},
            {"menu.uninstall_local", "Désinstaller pacterm"},
            {"main_menu.settings", "Paramètres"},
            {"menu.settings", "Paramètres"},
            {"main_menu.quit", "Quitter"},
            {"menu.exit", "Quitter"},

            {"settings.title", "PARAMÈTRES"},
            {"settings.general_theme", "Thème général"},
            {"settings.language", "Langue"},
            {"settings.nerd_fonts", "Nerd Fonts"},
            {"settings.sound", "Son"},
            {"settings.pacman_theme", "Thème Pac-Man"},
            {"settings.key_config", "Configurer touches"},
            {"settings.reset", "Réinitialiser"},
            {"settings.back", "Retour au menu"},
            {"settings.on", "ACTIVÉ"},
            {"settings.off", "DÉSACTIVÉ"},
            {"settings.reset_success", "Tout a été réinitialisé avec succès !"},

            {"pause.title", "JEU EN PAUSE"},
            {"pause.resume", "Reprendre la partie"},
            {"pause.theme_info", "Guide des thèmes"},
            {"pause.restart", "Recommencer niveau"},
            {"pause.return_menu", "Retour au menu"},
            {"pause.quit", "Quitter le jeu"},

            {"hud.shield", "BOUCLIER ACTIF"},
            {"hud.double_bounty", "PRIME x2: {}s"},
            {"hud.bounty", "PRIME x2: {}s"},
            {"hud.fever", "FIÈVRE x2: {}s"},
            {"hud.freeze", "GEL: {}s"},
            {"hud.score", " SCORE: {}"},
            {"hud.score_anon", " SCORE: {}"},
            {"hud.high", "RECORD: {} "},
            {"hud.level", "NIVEAU {} "},
            {"hud.lives", " VIES: "},
            {"hud.muted", " [MUET]"},
            {"hud.x2_score", "SCORE x2: {}s"},
            {"hud.speed", "VITESSE: {}s"},
            {"hud.magnet", "AIMANT: {}s"},

            {"game.ready", "PRÊT !"},
            {"game.game_over", "PARTIE TERMINÉE"},
            {"game.press_continue", "Appuyez sur ENTRÉE pour revenir"},
            {"game.press_any_key", "Appuyez sur une touche pour revenir"},

            {"stats.title", "STATISTIQUES"},
            {"stats.high_score", "Meilleur score"},
            {"stats.games_played", "Parties jouées"},
            {"stats.ghosts_eaten", "Fantômes mangés"},
            {"stats.deaths", "Morts"},
            {"stats.best_level", "Meilleur niveau"},
            {"stats.dots_eaten", "Points mangés"},
            {"stats.power_pellets", "Super pastilles"},
            {"stats.time_played", "Temps de jeu"},
            {"stats.letters", "Lettres"},
            {"stats.letter", "Lettres"},

            {"key_config.title", "CONFIGURATION TOUCHES"},
            {"key_config.up", "HAUT:   [ {} ]"},
            {"key_config.down", "BAS:    [ {} ]"},
            {"key_config.left", "GAUCHE: [ {} ]"},
            {"key_config.right", "DROITE: [ {} ]"},
            {"key_config.pause", "PAUSE:  [ {} ]"},
            {"key_config.press_key", "TOUCHE..."},
            {"key_config.save_back", "Sauver & Retour"},
            {"key_config.binding_prompt", "APPUYEZ SUR UNE TOUCHE (ÉCHAP: ANNULER)"},
            {"key_config.enter_prompt", "ENTRÉE pour modifier"},

            {"level_select.title", "CHOISIR UN NIVEAU"},
            {"level_select.back", "RETOUR AU MENU"},

            {"theme_info.title", "GUIDE DES THÈMES"},
            {"theme_info.active_mechanics", "Mécaniques actives :"},
            {"theme_info.theme", "Thème : "},
            {"theme_info.hazard_prefix", "• Danger : "},
            {"theme_info.t0_m1", "Rythme arcade standard et IA équilibrée"},
            {"theme_info.t0_m2", "Multiplicateur de score sur combos"},
            {"theme_info.t0_hz", "Goulots d'étranglement étroits"},
            {"theme_info.t1_m1", "Surcroît de vitesse : Pac-Man +20% rapide"},
            {"theme_info.t1_m2", "Traînées acides : résidus toxiques au sol"},
            {"theme_info.t1_hz", "Couloirs étroits dangereux"},
            {"theme_info.t2_m1", "Étourdissement : tunnels étourdissent fantômes"},
            {"theme_info.t2_m2", "Prime de points : bonus sur séries"},
            {"theme_info.t2_hz", "Traque fantôme à grande vitesse"},
            {"theme_info.t3_m1", "Bouclier de braise : immunité thermique"},
            {"theme_info.t3_m2", "Multiplicateur magma : points doublés"},
            {"theme_info.t3_hz", "Cases de lave s'enflamment régulièrement"},
            {"theme_info.t4_m1", "Ruée : Appuyez sur [ESPACE] pour foncer"},
            {"theme_info.t4_m2", "Combos fureur : x4 sur chaînes de fantômes"},
            {"theme_info.t4_hz", "Poursuite agressive des fantômes"},
            {"theme_info.t5_m1", "Frénésie : vitesse de dévoration accrue"},
            {"theme_info.t5_m2", "Récolte arcanique : attire les pastilles"},
            {"theme_info.t5_hz", "Téléportations instables"},
            {"theme_info.t6_m1", "Gel prolongé : durée de gel étendue"},
            {"theme_info.t6_m2", "Aura glacée : ralentit les fantômes proches"},
            {"theme_info.t6_hz", "Inertie glacée : glissade aux virages"},
            {"theme_info.t7_m1", "Bouclier fondu : invincibilité sur fruits"},
            {"theme_info.t7_m2", "Multiplicateur doré : x4 sur fantômes"},
            {"theme_info.t7_hz", "Fantômes en frénésie permanente"},
            {"theme_info.t8_m1", "Spectre prismatique : frénésie multicolore"},
            {"theme_info.t8_m2", "Multiplicateur continu de pastilles"},
            {"theme_info.t8_hz", "Accélération chromatique des fantômes"},
            {"theme_info.t9_m1", "Chance du chaos : bonus surprises aléatoires"},
            {"theme_info.t9_m2", "Warp binaire : tunnels imprévisibles"},
            {"theme_info.t9_hz", "Corruption et bruit visuel des murs"},
            {"theme_info.t10_m1", "Thème Maître : combine tous les bonus"},
            {"theme_info.t10_m2", "Score prismatique : bonus permanent x1.5"},
            {"theme_info.t10_hz", "Courbe de difficulté fantôme ultime"},

            {"redeem.title", "CODE CADEAU"},
            {"redeem.hint", "ÉCHAP: Annuler  ENTRÉE: Valider"},

            {"username.title", "PSEUDO"},
            {"username.hint", "ÉCHAP: Annuler  ENTRÉE: Valider"},

            {"common.on", "ACTIVÉ"},
            {"common.off", "DÉSACTIVÉ"},
        };

        const std::unordered_map<std::string_view, std::string_view> kDictEs = {
            {"main_menu.start", "Iniciar juego"},
            {"menu.start_game", "Iniciar juego"},
            {"main_menu.username", "Usuario"},
            {"menu.username", "Usuario:        {}"},
            {"menu.username_short", "Usuario: {}"},
            {"main_menu.stats", "Estadísticas"},
            {"menu.stats", "Estadísticas"},
            {"main_menu.redeem", "Canjear código"},
            {"menu.redeem_code", "Canjear código"},
            {"main_menu.install", "Instalar pacterm"},
            {"menu.install", "Instalar pacterm"},
            {"menu.install_local", "Instalar pacterm localmente"},
            {"main_menu.uninstall", "Desinstalar pacterm"},
            {"menu.uninstall", "Desinstalar pacterm"},
            {"menu.uninstall_local", "Desinstalar pacterm"},
            {"main_menu.settings", "Ajustes"},
            {"menu.settings", "Ajustes"},
            {"main_menu.quit", "Salir"},
            {"menu.exit", "Salir"},

            {"settings.title", "AJUSTES"},
            {"settings.general_theme", "Tema general"},
            {"settings.language", "Idioma"},
            {"settings.nerd_fonts", "Nerd Fonts"},
            {"settings.sound", "Sonido"},
            {"settings.pacman_theme", "Tema Pac-Man"},
            {"settings.key_config", "Configurar teclas"},
            {"settings.reset", "Restablecer"},
            {"settings.back", "Volver al menú"},
            {"settings.on", "SÍ"},
            {"settings.off", "NO"},
            {"settings.reset_success", "¡Todo se restableció con éxito!"},

            {"pause.title", "JUEGO EN PAUSA"},
            {"pause.resume", "Reanudar juego"},
            {"pause.theme_info", "Guía de temas"},
            {"pause.restart", "Reiniciar nivel"},
            {"pause.return_menu", "Volver al menú"},
            {"pause.quit", "Salir del juego"},

            {"hud.shield", "ESCUDO ACTIVO"},
            {"hud.double_bounty", "2x PUNTOS: {}s"},
            {"hud.bounty", "2x PUNTOS: {}s"},
            {"hud.fever", "FIEBRE x2: {}s"},
            {"hud.freeze", "CONGELADO: {}s"},
            {"hud.score", " PUNTOS: {}"},
            {"hud.score_anon", " PUNTOS: {}"},
            {"hud.high", "RÉCORD: {} "},
            {"hud.level", "NIVEL {} "},
            {"hud.lives", " VIDAS: "},
            {"hud.muted", " [SILENCIO]"},
            {"hud.x2_score", "PUNTOS x2: {}s"},
            {"hud.speed", "VELOCIDAD: {}s"},
            {"hud.magnet", "IMÁN: {}s"},

            {"game.ready", "¡LISTO!"},
            {"game.game_over", "FIN DEL JUEGO"},
            {"game.press_continue", "Pulsa ENTER para volver al menú"},
            {"game.press_any_key", "Pulsa cualquier tecla para volver"},

            {"stats.title", "ESTADÍSTICAS"},
            {"stats.high_score", "Récord"},
            {"stats.games_played", "Partidas"},
            {"stats.ghosts_eaten", "Fantasmas"},
            {"stats.deaths", "Muertes"},
            {"stats.best_level", "Mejor nivel"},
            {"stats.dots_eaten", "Puntos comidos"},
            {"stats.power_pellets", "Superpuntos"},
            {"stats.time_played", "Tiempo de juego"},
            {"stats.letters", "Letras"},
            {"stats.letter", "Letras"},

            {"key_config.title", "CONFIGURAR TECLAS"},
            {"key_config.up", "ARRIBA:    [ {} ]"},
            {"key_config.down", "ABAJO:     [ {} ]"},
            {"key_config.left", "IZQUIERDA: [ {} ]"},
            {"key_config.right", "DERECHA:   [ {} ]"},
            {"key_config.pause", "PAUSA:     [ {} ]"},
            {"key_config.press_key", "PULSA TECLA..."},
            {"key_config.save_back", "Guardar y Volver"},
            {"key_config.binding_prompt", "PULSA UNA TECLA (ESC: CANCELAR)"},
            {"key_config.enter_prompt", "Pulsa ENTER para editar"},

            {"level_select.title", "ELEGIR NIVEL"},
            {"level_select.back", "VOLVER AL MENÚ"},

            {"theme_info.title", "GUÍA DE TEMAS"},
            {"theme_info.active_mechanics", "Mecánicas activas:"},
            {"theme_info.theme", "Tema: "},
            {"theme_info.hazard_prefix", "• Peligro: "},
            {"theme_info.t0_m1", "Ritmo arcade clásico e IA equilibrada"},
            {"theme_info.t0_m2", "Multiplicador por racha continua de puntos"},
            {"theme_info.t0_hz", "Pasillos estrechos peligrosos"},
            {"theme_info.t1_m1", "Oleada de velocidad: Pac-Man 20% más rápido"},
            {"theme_info.t1_m2", "Rastro ácido: residuos tóxicos al avanzar"},
            {"theme_info.t1_hz", "Pasillos estrechos peligrosos"},
            {"theme_info.t2_m1", "Aturdimiento: túneles aturden fantasmas"},
            {"theme_info.t2_m2", "Botín de puntos: bonus por rachas"},
            {"theme_info.t2_hz", "Persecución veloz en túneles"},
            {"theme_info.t3_m1", "Escudo ígneo: inmunidad térmica en suelo"},
            {"theme_info.t3_m2", "Multiplicador magma: puntos dobles en lava"},
            {"theme_info.t3_hz", "Suelo de lava se inflama periódicamente"},
            {"theme_info.t4_m1", "Impulso: Pulsa [ESPACIO] para acelerar"},
            {"theme_info.t4_m2", "Combos furia: x4 al encadenar fantasmas"},
            {"theme_info.t4_hz", "Persecución agresiva de fantasmas"},
            {"theme_info.t5_m1", "Frenesí voraz: come puntos velozmente"},
            {"theme_info.t5_m2", "Cosecha arcana: atrae puntos distantes"},
            {"theme_info.t5_hz", "Puntos de teletransporte inestables"},
            {"theme_info.t6_m1", "Permafrost: congelación extendida"},
            {"theme_info.t6_m2", "Aura gélida: ralentiza fantasmas cercanos"},
            {"theme_info.t6_hz", "Fricción helada: inercia en giros"},
            {"theme_info.t7_m1", "Escudo dorado: invulnerabilidad con fruta"},
            {"theme_info.t7_m2", "Multiplicador oro: x4 comiendo fantasmas"},
            {"theme_info.t7_hz", "Fantasmas en frenesí constante"},
            {"theme_info.t8_m1", "Espectro cromático: colores dinámicos"},
            {"theme_info.t8_m2", "Multiplicador continuo de puntos"},
            {"theme_info.t8_hz", "Aceleración cromática enemiga"},
            {"theme_info.t9_m1", "Caos y suerte: ventajas sorpresa aleatorias"},
            {"theme_info.t9_m2", "Salto binario: túneles impredecibles"},
            {"theme_info.t9_hz", "Corrupción visual en paredes"},
            {"theme_info.t10_m1", "Tema Maestro: combina todos los beneficios"},
            {"theme_info.t10_m2", "Puntuación prismática: bonus x1.5 perenne"},
            {"theme_info.t10_hz", "Dificultad suprema de fantasmas"},

            {"redeem.title", "CANJEAR CÓDIGO"},
            {"redeem.hint", "ESC: Cancelar  ENTER: Canjear"},

            {"username.title", "USUARIO"},
            {"username.hint", "ESC: Cancelar  ENTER: Guardar"},

            {"common.on", "SÍ"},
            {"common.off", "NO"},
        };

        const std::unordered_map<std::string_view, std::string_view> kDictDe = {
            {"main_menu.start", "Spiel starten"},
            {"menu.start_game", "Spiel starten"},
            {"main_menu.username", "Benutzername"},
            {"menu.username", "Benutzername:   {}"},
            {"menu.username_short", "Benutzer: {}"},
            {"main_menu.stats", "Statistiken"},
            {"menu.stats", "Statistiken"},
            {"main_menu.redeem", "Code einlösen"},
            {"menu.redeem_code", "Code einlösen"},
            {"main_menu.install", "pacterm installieren"},
            {"menu.install", "pacterm installieren"},
            {"menu.install_local", "pacterm lokal installieren"},
            {"main_menu.uninstall", "pacterm entfernen"},
            {"menu.uninstall", "pacterm entfernen"},
            {"menu.uninstall_local", "pacterm entfernen"},
            {"main_menu.settings", "Einstellungen"},
            {"menu.settings", "Einstellungen"},
            {"main_menu.quit", "Beenden"},
            {"menu.exit", "Beenden"},

            {"settings.title", "EINSTELLUNGEN"},
            {"settings.general_theme", "Hauptdesign"},
            {"settings.language", "Sprache"},
            {"settings.nerd_fonts", "Nerd Fonts"},
            {"settings.sound", "Ton"},
            {"settings.pacman_theme", "Pac-Man Design"},
            {"settings.key_config", "Tastenbelegung"},
            {"settings.reset", "Zurücksetzen"},
            {"settings.back", "Hauptmenü"},
            {"settings.on", "AN"},
            {"settings.off", "AUS"},
            {"settings.reset_success", "Alles erfolgreich zurückgesetzt!"},

            {"pause.title", "SPIEL PAUSIERT"},
            {"pause.resume", "Fortsetzen"},
            {"pause.theme_info", "Design-Info"},
            {"pause.restart", "Level neustarten"},
            {"pause.return_menu", "Hauptmenü"},
            {"pause.quit", "Spiel beenden"},

            {"hud.shield", "SCHILD AKTIV"},
            {"hud.double_bounty", "2x BONUS: {}s"},
            {"hud.bounty", "2x BONUS: {}s"},
            {"hud.fever", "FIEBER x2: {}s"},
            {"hud.freeze", "FROST: {}s"},
            {"hud.score", " PUNKTE: {}"},
            {"hud.score_anon", " PUNKTE: {}"},
            {"hud.high", "REKORD: {} "},
            {"hud.level", "STUFE {} "},
            {"hud.lives", " LEBEN: "},
            {"hud.muted", " [STUMM]"},
            {"hud.x2_score", "x2 PUNKTE: {}s"},
            {"hud.speed", "TEMPO: {}s"},
            {"hud.magnet", "MAGNET: {}s"},

            {"game.ready", "BEREIT!"},
            {"game.game_over", "SPIEL VORBEI"},
            {"game.press_continue", "ENTER drücken für Hauptmenü"},
            {"game.press_any_key", "Beliebige Taste drücken"},

            {"stats.title", "STATISTIKEN"},
            {"stats.high_score", "Rekord"},
            {"stats.games_played", "Spiele"},
            {"stats.ghosts_eaten", "Geister"},
            {"stats.deaths", "Tode"},
            {"stats.best_level", "Beste Stufe"},
            {"stats.dots_eaten", "Punkte"},
            {"stats.power_pellets", "Power-Punkte"},
            {"stats.time_played", "Spielzeit"},
            {"stats.letters", "Buchstaben"},
            {"stats.letter", "Buchstaben"},

            {"key_config.title", "TASTENBELEGUNG"},
            {"key_config.up", "HOCH:   [ {} ]"},
            {"key_config.down", "RUNTER: [ {} ]"},
            {"key_config.left", "LINKS:  [ {} ]"},
            {"key_config.right", "RECHTS: [ {} ]"},
            {"key_config.pause", "PAUSE:  [ {} ]"},
            {"key_config.press_key", "TASTE DRÜCKEN..."},
            {"key_config.save_back", "Speichern & Zurück"},
            {"key_config.binding_prompt", "TASTE DRÜCKEN (ESC: ABBRECHEN)"},
            {"key_config.enter_prompt", "ENTER zum Bearbeiten"},

            {"level_select.title", "STUFE WÄHLEN"},
            {"level_select.back", "HAUPTMENÜ"},

            {"theme_info.title", "DESIGN-INFO"},
            {"theme_info.active_mechanics", "Aktive Mechaniken:"},
            {"theme_info.theme", "Design: "},
            {"theme_info.hazard_prefix", "• Gefahr: "},
            {"theme_info.t0_m1", "Klassisches Arcade-Tempo und Geister-KI"},
            {"theme_info.t0_m2", "Punkteserien-Multiplikator"},
            {"theme_info.t0_hz", "Engpässe in Korridoren"},
            {"theme_info.t1_m1", "Temposchub: Pac-Man 20% schneller"},
            {"theme_info.t1_m2", "Säurespuren: giftige Rückstände am Boden"},
            {"theme_info.t1_hz", "Enge Korridorpassagen"},
            {"theme_info.t2_m1", "Tunnel-Betäubung: lähmt Geister im Tunnel"},
            {"theme_info.t2_m2", "Punktprämie: Extra-Punkte für Serien"},
            {"theme_info.t2_hz", "Schnelle Geisterverfolgung im Tunnel"},
            {"theme_info.t3_m1", "Glut-Schild: Hitze-Immunität am Boden"},
            {"theme_info.t3_m2", "Magma-Multiplikator: doppelte Punkte"},
            {"theme_info.t3_hz", "Lavafelder entflammen periodisch"},
            {"theme_info.t4_m1", "Sprint: [LEERTASTE] für Vorwärtsschub"},
            {"theme_info.t4_m2", "Wut-Combos: 4x Punkte für Geisterketten"},
            {"theme_info.t4_hz", "Aggressive Verfolgungs-Algorithmen"},
            {"theme_info.t5_m1", "Blitz-Prämie: rasantes Fressen von Punkten"},
            {"theme_info.t5_m2", "Arkane Ernte: zieht entfernte Punkte an"},
            {"theme_info.t5_hz", "Instabile Teleportationsspitzen"},
            {"theme_info.t6_m1", "Dauerfrost: verlängerte Geister-Starre"},
            {"theme_info.t6_m2", "Frost-Aura: bremst nahe Geister"},
            {"theme_info.t6_hz", "Eisige Trägheit: Rutschen in Kurven"},
            {"theme_info.t7_m1", "Glut-Schild: Unverwundbarkeit durch Frucht"},
            {"theme_info.t7_m2", "Gold-Multiplikator: 4x Punkte für Geister"},
            {"theme_info.t7_hz", "Dauerhafter Geister-Rausch"},
            {"theme_info.t8_m1", "Prismenspektrum: dynamischer Farbenrausch"},
            {"theme_info.t8_m2", "Fortlaufender Punkteserien-Bonus"},
            {"theme_info.t8_hz", "Farbbeschleunigung der Geister"},
            {"theme_info.t9_m1", "Chaos-Glück: Überraschungs-Powerups"},
            {"theme_info.t9_m2", "Binär-Warp: unberechenbare Tunnelsprünge"},
            {"theme_info.t9_hz", "Flackernde Wandkorruption"},
            {"theme_info.t10_m1", "Meisterdesign: vereint alle Stärkungen"},
            {"theme_info.t10_m2", "Prismen-Punkte: dauerhafter 1.5x Bonus"},
            {"theme_info.t10_hz", "Höchste Geister-Schwierigkeit"},

            {"redeem.title", "CODE EINLÖSEN"},
            {"redeem.hint", "ESC: Abbrechen  ENTER: Einlösen"},

            {"username.title", "BENUTZERNAME"},
            {"username.hint", "ESC: Abbrechen  ENTER: Setzen"},

            {"common.on", "AN"},
            {"common.off", "AUS"},
        };

        const std::unordered_map<std::string_view, std::string_view> kDictIt = {
            {"main_menu.start", "Inizia gioco"},
            {"menu.start_game", "Inizia gioco"},
            {"main_menu.username", "Nome utente"},
            {"menu.username", "Utente:         {}"},
            {"menu.username_short", "Utente: {}"},
            {"main_menu.stats", "Statistiche"},
            {"menu.stats", "Statistiche"},
            {"main_menu.redeem", "Riscatta codice"},
            {"menu.redeem_code", "Riscatta codice"},
            {"main_menu.install", "Installa pacterm"},
            {"menu.install", "Installa pacterm"},
            {"menu.install_local", "Installa pacterm localmente"},
            {"main_menu.uninstall", "Disinstalla pacterm"},
            {"menu.uninstall", "Disinstalla pacterm"},
            {"menu.uninstall_local", "Disinstalla pacterm"},
            {"main_menu.settings", "Impostazioni"},
            {"menu.settings", "Impostazioni"},
            {"main_menu.quit", "Esci"},
            {"menu.exit", "Esci"},

            {"settings.title", "IMPOSTAZIONI"},
            {"settings.general_theme", "Tema generale"},
            {"settings.language", "Lingua"},
            {"settings.nerd_fonts", "Nerd Fonts"},
            {"settings.sound", "Audio"},
            {"settings.pacman_theme", "Tema Pac-Man"},
            {"settings.key_config", "Configura tasti"},
            {"settings.reset", "Ripristina"},
            {"settings.back", "Torna al menu"},
            {"settings.on", "SÌ"},
            {"settings.off", "NO"},
            {"settings.reset_success", "Tutto reimpostato con successo!"},

            {"pause.title", "IN PAUSA"},
            {"pause.resume", "Riprendi gioco"},
            {"pause.theme_info", "Guida temi"},
            {"pause.restart", "Riavvia livello"},
            {"pause.return_menu", "Torna al menu"},
            {"pause.quit", "Esci dal gioco"},

            {"hud.shield", "SCUDO ATTIVO"},
            {"hud.double_bounty", "2x PUNTI: {}s"},
            {"hud.bounty", "2x PUNTI: {}s"},
            {"hud.fever", "FEBBRE x2: {}s"},
            {"hud.freeze", "GELO: {}s"},
            {"hud.score", " PUNTI: {}"},
            {"hud.score_anon", " PUNTOS: {}"},
            {"hud.high", "RECORD: {} "},
            {"hud.level", "LIVELLO {} "},
            {"hud.lives", " VITE: "},
            {"hud.muted", " [MUTO]"},
            {"hud.x2_score", "PUNTI x2: {}s"},
            {"hud.speed", "VELOCITÀ: {}s"},
            {"hud.magnet", "CALAMITA: {}s"},

            {"game.ready", "PRONTI!"},
            {"game.game_over", "PARTITA FINITA"},
            {"game.press_continue", "Premi INVIO per tornare al menu"},
            {"game.press_any_key", "Premi un tasto per tornare"},

            {"stats.title", "STATISTICHE"},
            {"stats.high_score", "Miglior punteggio"},
            {"stats.games_played", "Partite giocate"},
            {"stats.ghosts_eaten", "Fantasmi mangiati"},
            {"stats.deaths", "Morti"},
            {"stats.best_level", "Miglior livello"},
            {"stats.dots_eaten", "Punti mangiati"},
            {"stats.power_pellets", "Super pillole"},
            {"stats.time_played", "Tempo giocato"},
            {"stats.letters", "Lettere"},
            {"stats.letter", "Lettere"},

            {"key_config.title", "CONFIGURA TASTI"},
            {"key_config.up", "SU:     [ {} ]"},
            {"key_config.down", "GIÙ:    [ {} ]"},
            {"key_config.left", "SINISTRA:[ {} ]"},
            {"key_config.right", "DESTRA: [ {} ]"},
            {"key_config.pause", "PAUSA:  [ {} ]"},
            {"key_config.press_key", "PREMI TASTO..."},
            {"key_config.save_back", "Salva e Torna"},
            {"key_config.binding_prompt", "PREMI UN TASTO (ESC: ANNULLA)"},
            {"key_config.enter_prompt", "Premi INVIO per modificare"},

            {"level_select.title", "SCEGLI LIVELLO"},
            {"level_select.back", "TORNA AL MENU"},

            {"theme_info.title", "GUIDA TEMI"},
            {"theme_info.active_mechanics", "Meccaniche attive:"},
            {"theme_info.theme", "Tema: "},
            {"theme_info.hazard_prefix", "• Pericolo: "},
            {"theme_info.t0_m1", "Ritmo arcade classico e IA bilanciata"},
            {"theme_info.t0_m2", "Moltiplicatore su serie di punti"},
            {"theme_info.t0_hz", "Corridoi stretti rischiosi"},
            {"theme_info.t1_m1", "Scatto rapido: Pac-Man +20% veloce"},
            {"theme_info.t1_m2", "Scia acida: residui tossici sul percorso"},
            {"theme_info.t1_hz", "Stretti punti di passaggio"},
            {"theme_info.t2_m1", "Stordimento: tunnel stordiscono fantasmi"},
            {"theme_info.t2_m2", "Bottino punti: bonus per serie continue"},
            {"theme_info.t2_hz", "Inseguimento veloce nei tunnel"},
            {"theme_info.t3_m1", "Scudo termico: immunità sui pavimenti caldi"},
            {"theme_info.t3_m2", "Moltiplicatore magma: doppi punti su lava"},
            {"theme_info.t3_hz", "Caselle di lava si infiammano spesso"},
            {"theme_info.t4_m1", "Scatto: Premi [SPAZIO] per accelerare"},
            {"theme_info.t4_m2", "Combo furia: 4x punti su catene fantasmi"},
            {"theme_info.t4_hz", "Algoritmi di inseguimento aggressivi"},
            {"theme_info.t5_m1", "Frenesia: velocità mangia-palline aumentata"},
            {"theme_info.t5_m2", "Raccolta arcana: attrae palline lontane"},
            {"theme_info.t5_hz", "Teletrasporti instabili"},
            {"theme_info.t6_m1", "Permafrost: congelamento prolungato"},
            {"theme_info.t6_m2", "Aura gelida: rallenta fantasmi vicini"},
            {"theme_info.t6_hz", "Attrito ghiacciato: scivolamento in curva"},
            {"theme_info.t7_m1", "Scudo dorato: invulnerabilità con frutti"},
            {"theme_info.t7_m2", "Moltiplicatore oro: 4x punti sui fantasmi"},
            {"theme_info.t7_hz", "Fantasmi sempre al doppio della velocità"},
            {"theme_info.t8_m1", "Spettro prismatico: colori dinamici"},
            {"theme_info.t8_m2", "Moltiplicatore continuo sui punti"},
            {"theme_info.t8_hz", "Accelerazione cromatica dei fantasmi"},
            {"theme_info.t9_m1", "Caos e fortuna: potenziamenti a sorpresa"},
            {"theme_info.t9_m2", "Warp binario: tunnel imprevedibili"},
            {"theme_info.t9_hz", "Corruzione visiva delle pareti"},
            {"theme_info.t10_m1", "Tema Maestro: combina tutti i benefici"},
            {"theme_info.t10_m2", "Punteggio prismatico: bonus 1.5x fisso"},
            {"theme_info.t10_hz", "Difficoltà suprema dei fantasmi"},

            {"redeem.title", "RISCATTA CODICE"},
            {"redeem.hint", "ESC: Annulla  INVIO: Riscatta"},

            {"username.title", "NOME UTENTE"},
            {"username.hint", "ESC: Annulla  INVIO: Salva"},

            {"common.on", "SÌ"},
            {"common.off", "NO"},
        };

        const std::unordered_map<std::string_view, std::string_view> kDictJa = {
            {"main_menu.start", "ゲーム開始"},
            {"menu.start_game", "ゲーム開始"},
            {"main_menu.username", "ユーザー名"},
            {"menu.username", "ユーザー名:     {}"},
            {"menu.username_short", "ユーザー名: {}"},
            {"main_menu.stats", "戦績"},
            {"menu.stats", "戦績"},
            {"main_menu.redeem", "コード入力"},
            {"menu.redeem_code", "コード入力"},
            {"main_menu.install", "ローカルにインストール"},
            {"menu.install", "ローカルにインストール"},
            {"menu.install_local", "ローカルにインストール"},
            {"main_menu.uninstall", "アンインストール"},
            {"menu.uninstall", "アンインストール"},
            {"menu.uninstall_local", "アンインストール"},
            {"main_menu.settings", "設定"},
            {"menu.settings", "設定"},
            {"main_menu.quit", "終了"},
            {"menu.exit", "終了"},

            {"settings.title", "設定"},
            {"settings.general_theme", "全体テーマ"},
            {"settings.language", "言語"},
            {"settings.nerd_fonts", "Nerd Fonts"},
            {"settings.sound", "効果音"},
            {"settings.pacman_theme", "パックマン色"},
            {"settings.key_config", "キー設定"},
            {"settings.reset", "初期化"},
            {"settings.back", "メニューに戻る"},
            {"settings.on", "ON"},
            {"settings.off", "OFF"},
            {"settings.reset_success", "すべての設定が初期化されました！"},

            {"pause.title", "ポーズ"},
            {"pause.resume", "再開"},
            {"pause.theme_info", "テーマ情報"},
            {"pause.restart", "やり直す"},
            {"pause.return_menu", "メニューへ"},
            {"pause.quit", "ゲーム終了"},

            {"hud.shield", "シールド発動中"},
            {"hud.double_bounty", "2倍スコア: {}秒"},
            {"hud.bounty", "2倍スコア: {}秒"},
            {"hud.fever", "フィーバー: {}秒"},
            {"hud.freeze", "フリーズ: {}秒"},
            {"hud.score", " スコア: {}"},
            {"hud.score_anon", " スコア: {}"},
            {"hud.high", "ハイスコア: {} "},
            {"hud.level", "レベル {} "},
            {"hud.lives", " 残機: "},
            {"hud.muted", " [消音]"},
            {"hud.x2_score", "スコア2倍: {}秒"},
            {"hud.speed", "加速: {}秒"},
            {"hud.magnet", "磁石: {}秒"},

            {"game.ready", "レディ！"},
            {"game.game_over", "ゲームオーバー"},
            {"game.press_continue", "ENTERキーでメニューに戻る"},
            {"game.press_any_key", "何かキーを押して戻る"},

            {"stats.title", "戦績"},
            {"stats.high_score", "ハイスコア"},
            {"stats.games_played", "プレイ回数"},
            {"stats.ghosts_eaten", "食べたゴースト"},
            {"stats.deaths", "ミス回数"},
            {"stats.best_level", "最高到達レベル"},
            {"stats.dots_eaten", "食べたドット"},
            {"stats.power_pellets", "パワーエサ"},
            {"stats.time_played", "プレイ時間"},
            {"stats.letters", "文字収集"},
            {"stats.letter", "文字収集"},

            {"key_config.title", "キー設定"},
            {"key_config.up", "上:   [ {} ]"},
            {"key_config.down", "下:   [ {} ]"},
            {"key_config.left", "左:   [ {} ]"},
            {"key_config.right", "右:   [ {} ]"},
            {"key_config.pause", "一時停止: [ {} ]"},
            {"key_config.press_key", "キー入力待ち..."},
            {"key_config.save_back", "保存して戻る"},
            {"key_config.binding_prompt", "キーを押してください (ESC: 取消)"},
            {"key_config.enter_prompt", "ENTERで編集"},

            {"level_select.title", "レベル選択"},
            {"level_select.back", "メニューに戻る"},

            {"theme_info.title", "テーマ情報"},
            {"theme_info.active_mechanics", "有効な効果:"},
            {"theme_info.theme", "テーマ: "},
            {"theme_info.hazard_prefix", "• 危険: "},
            {"theme_info.t0_m1", "標準的なアーケード設定とAIバランス"},
            {"theme_info.t0_m2", "連続ドット取得コンボ倍率"},
            {"theme_info.t0_hz", "狭い通路のチョークポイント"},
            {"theme_info.t1_m1", "スピードサージ: パックマン速度+20%"},
            {"theme_info.t1_m2", "アシッドトレイル: 通過後に有毒床"},
            {"theme_info.t1_hz", "狭い通路の危険地帯"},
            {"theme_info.t2_m1", "ワープスタン: トンネル通過で敵スタン"},
            {"theme_info.t2_m2", "ドットバウンティ: 連続獲得ボーナス"},
            {"theme_info.t2_hz", "トンネル内の高速追跡"},
            {"theme_info.t3_m1", "耐熱シールド: 熱床のダメージ無効"},
            {"theme_info.t3_m2", "マグマ倍率: 溶岩上の得点2倍"},
            {"theme_info.t3_hz", "溶岩床が定期的に発火"},
            {"theme_info.t4_m1", "ダッシュ: [SPACE]で前方に急加速"},
            {"theme_info.t4_m2", "フューリーコンボ: ゴースト連続で4倍"},
            {"theme_info.t4_hz", "積極的な追跡アルゴリズム"},
            {"theme_info.t5_m1", "ブリッツバウンティ: 捕食スピード上昇"},
            {"theme_info.t5_m2", "アーケインハーベスト: 遠くのドットを吸引"},
            {"theme_info.t5_hz", "不安定なテレポート"},
            {"theme_info.t6_m1", "永久凍土: フリーズ持続時間延長"},
            {"theme_info.t6_m2", "冷却オーラ: 周囲のゴースト減速"},
            {"theme_info.t6_hz", "氷の摩擦: 旋回時の慣性滑り"},
            {"theme_info.t7_m1", "溶融シールド: フルーツ獲得で無敵"},
            {"theme_info.t7_m2", "ゴールデン倍率: ゴースト得点4倍"},
            {"theme_info.t7_hz", "常時倍速ゴースト暴走"},
            {"theme_info.t8_m1", "プリズムスペクトル: 多彩な色彩効果"},
            {"theme_info.t8_m2", "連続ドット倍率スコア"},
            {"theme_info.t8_hz", "ゴーストの加速"},
            {"theme_info.t9_m1", "カオスラック: ランダムな効果発生"},
            {"theme_info.t9_m2", "バイナリワープ: 予測不能なトンネル"},
            {"theme_info.t9_hz", "壁のノイズと画面乱れ"},
            {"theme_info.t10_m1", "マスターテーマ: 全ての強化効果を統合"},
            {"theme_info.t10_m2", "プリズムスコア: 常時1.5倍スコア"},
            {"theme_info.t10_hz", "最高峰のゴースト難易度"},

            {"redeem.title", "コード入力"},
            {"redeem.hint", "ESC: 取消  ENTER: 適用"},

            {"username.title", "ユーザー名"},
            {"username.hint", "ESC: 取消  ENTER: 設定"},

            {"common.on", "ON"},
            {"common.off", "OFF"},
        };

        const std::unordered_map<std::string_view, std::string_view> kDictAr = {
            {"main_menu.start", "Ibda2 Al-Lo3ba"},
            {"menu.start_game", "Ibda2 Al-Lo3ba"},
            {"main_menu.username", "Ism Al-Mustakhdim"},
            {"menu.username", "Al-Mustakhdim:  {}"},
            {"menu.username_short", "Al-Mustakhdim: {}"},
            {"main_menu.stats", "Al-Ihsa'iyat"},
            {"menu.stats", "Al-Ihsa'iyat"},
            {"main_menu.redeem", "Istridad Ramz"},
            {"menu.redeem_code", "Istridad Ramz"},
            {"main_menu.install", "Tathbeet Mahaliyan"},
            {"menu.install", "Tathbeet Mahaliyan"},
            {"menu.install_local", "Tathbeet Mahaliyan"},
            {"main_menu.uninstall", "Ilgha' Al-Tathbeet"},
            {"menu.uninstall", "Ilgha' Al-Tathbeet"},
            {"menu.uninstall_local", "Ilgha' Al-Tathbeet"},
            {"main_menu.settings", "Al-I3dadat"},
            {"menu.settings", "Al-I3dadat"},
            {"main_menu.quit", "Khorooj"},
            {"menu.exit", "Khorooj"},

            {"settings.title", "AL-I3DADAT"},
            {"settings.general_theme", "Al-Mazhar Al-3am"},
            {"settings.language", "Al-Lugha"},
            {"settings.nerd_fonts", "Khotoot Nerd"},
            {"settings.sound", "Al-Sawt"},
            {"settings.pacman_theme", "Mazhar Pac-Man"},
            {"settings.key_config", "Dabt Al-Azrar"},
            {"settings.reset", "I3adat Al-Dabt"},
            {"settings.back", "Al-Roojoo3 Lil-Qa'ima"},
            {"settings.on", "Mofa33al"},
            {"settings.off", "Mo3attal"},
            {"settings.reset_success", "Tammat i3adat al-dabt binajah!"},

            {"pause.title", "IQAF MO'AQQAT"},
            {"pause.resume", "Isti'naf Al-Lo3ba"},
            {"pause.theme_info", "Daleel Al-Mazhar"},
            {"pause.restart", "I3adat Al-Marhala"},
            {"pause.return_menu", "Al-Roojoo3 Lil-Qa'ima"},
            {"pause.quit", "Inha' Al-Lo3ba"},

            {"hud.shield", "DIR3 NASHEET"},
            {"hud.double_bounty", "MODA3AF 2x: {}s"},
            {"hud.bounty", "MODA3AF 2x: {}s"},
            {"hud.fever", "HAMASA x2: {}s"},
            {"hud.freeze", "TAJMEED: {}s"},
            {"hud.score", " AL-NOQAT: {}"},
            {"hud.score_anon", " AL-NOQAT: {}"},
            {"hud.high", "A3LA NATIJA: {} "},
            {"hud.level", "AL-MARHALA {} "},
            {"hud.lives", " AL-MOHAWARAT: "},
            {"hud.muted", " [MAKTOM]"},
            {"hud.x2_score", "NOQAT x2: {}s"},
            {"hud.speed", "SOR3A: {}s"},
            {"hud.magnet", "MIGHNATIS: {}s"},

            {"game.ready", "ISTA3ID!"},
            {"game.game_over", "INTAHAT AL-LO3BA"},
            {"game.press_continue", "Idghat ENTER lil-roojoo3"},
            {"game.press_any_key", "Idghat ayy zirr lil-roojoo3"},

            {"stats.title", "AL-IHSA'IYAT"},
            {"stats.high_score", "A3la Nateeja"},
            {"stats.games_played", "3adad Al-Al3ab"},
            {"stats.ghosts_eaten", "Ashbah Ma'koola"},
            {"stats.deaths", "Al-Khasa'ir"},
            {"stats.best_level", "A3la Marhala"},
            {"stats.dots_eaten", "Noqat Ma'koola"},
            {"stats.power_pellets", "Habbat Al-Taqa"},
            {"stats.time_played", "Waqt Al-La3b"},
            {"stats.letters", "Al-Horoof"},
            {"stats.letter", "Al-Horoof"},

            {"key_config.title", "DABT AL-AZRAR"},
            {"key_config.up", "A3LA:   [ {} ]"},
            {"key_config.down", "ASFAL:  [ {} ]"},
            {"key_config.left", "YASAR:  [ {} ]"},
            {"key_config.right", "YAMEEN: [ {} ]"},
            {"key_config.pause", "IQAF:   [ {} ]"},
            {"key_config.press_key", "IDGHAT ZIRR..."},
            {"key_config.save_back", "Hifz wa Roojoo3"},
            {"key_config.binding_prompt", "IDGHAT AYY ZIRR AL'AN (ESC: ILGHA')"},
            {"key_config.enter_prompt", "Idghat ENTER lil-ta3deel"},

            {"level_select.title", "IKHTAR AL-MARHALA"},
            {"level_select.back", "AL-ROOJOO3 LIL-QA'IMA"},

            {"theme_info.title", "DALEEL AL-MAZHAR"},
            {"theme_info.active_mechanics", "Al-Aliyat Al-Nasheeta:"},
            {"theme_info.theme", "Al-Mazhar: "},
            {"theme_info.hazard_prefix", "• Khatar: "},
            {"theme_info.t0_m1", "Sor3at arcade qiyasiya wa tawazun al-ashbah"},
            {"theme_info.t0_m2", "Moda3if al-noqat al-motasalsila"},
            {"theme_info.t0_hz", "Mamaratt dayyiqa harija"},
            {"theme_info.t1_m1", "Indifa3 al-sor3a: Pac-Man asra3 bi 20%"},
            {"theme_info.t1_m2", "Masarat himdiya: baqaya samma bil-masar"},
            {"theme_info.t1_hz", "Mamaratt dayyiqa khatira"},
            {"theme_info.t2_m1", "Sa3q al-nafaq: al-anfaq tas3aq al-ashbah"},
            {"theme_info.t2_m2", "Mokafo'at al-noqat: 3alawat 3ala al-salasill"},
            {"theme_info.t2_hz", "Motaba3at al-ashbah al-saree3a bil-anfaq"},
            {"theme_info.t3_m1", "Dir3 al-jamr: mana3a kamila didd hararat al-ard"},
            {"theme_info.t3_m2", "Moda3if al-sohara: noqat moda3afa 3ala al-himam"},
            {"theme_info.t3_hz", "Balatat al-himam tashta3il dawriyan"},
            {"theme_info.t4_m1", "Al-Indifa3: idghat [SPACE] lil-indifa3 lil-amam"},
            {"theme_info.t4_m2", "Silsilat al-ghadab: 4x noqat 3ala akl al-ashbah"},
            {"theme_info.t4_hz", "Khawarizmiyat motarada hojoomiya"},
            {"theme_info.t5_m1", "Hamasat al-iltisaq: iltiham fa'iq lil-hoboob"},
            {"theme_info.t5_m2", "Al-Hasad al-sihri: jazb al-hoboob al-ba3eeda"},
            {"theme_info.t5_hz", "Masarat intiqal ghayr mostaqirra"},
            {"theme_info.t6_m1", "Al-Jaleed al-da'im: ziyadat moddat al-tajmeed"},
            {"theme_info.t6_m2", "Halat al-borooda: ibta' al-ashbah al-qareeba"},
            {"theme_info.t6_hz", "Ihtikak jaleedi: inzilaq athna' al-in3itaf"},
            {"theme_info.t7_m1", "Dir3 al-sohara: hasana 3ind akl al-fawakih"},
            {"theme_info.t7_m2", "Al-Moda3if al-dhahabi: 4x 3ind akl al-ashbah"},
            {"theme_info.t7_hz", "Ashbah bi-sor3a moda3afa da'ima"},
            {"theme_info.t8_m1", "Tayf qawsi: alwan dynamic motaghayyira"},
            {"theme_info.t8_m2", "Moda3if mostamirr lil-noqat"},
            {"theme_info.t8_hz", "Tasaaro3 mostamirr lil-ashbah"},
            {"theme_info.t9_m1", "Hazz al-fawda: mokafo'at mofaji'a 3ashwa'iya"},
            {"theme_info.t9_m2", "Intiqal thona'i: anfaq ghayr motawaqqa3a"},
            {"theme_info.t9_hz", "Tashweesh wa wameed 3ala al-jodran"},
            {"theme_info.t10_m1", "Al-Mazhar al-shamil: yajma3 koll al-ta3zeezat"},
            {"theme_info.t10_m2", "Al-Noqat al-manshooriya: 1.5x noqat da'ima"},
            {"theme_info.t10_hz", "A3la mostawa so3ooba lil-ashbah"},

            {"redeem.title", "ISTRIDAD RAMZ"},
            {"redeem.hint", "ESC: Ilgha'  ENTER: Ta'keed"},

            {"username.title", "ISM AL-MUSTAKHDIM"},
            {"username.hint", "ESC: Ilgha'  ENTER: Ta'yeen"},

            {"common.on", "Mofa33al"},
            {"common.off", "Mo3attal"},
        };

        const std::unordered_map<std::string_view, std::string_view>& getDictionary(Language lang) {
            switch (lang) {
            case Language::Fr: return kDictFr;
            case Language::Es: return kDictEs;
            case Language::De: return kDictDe;
            case Language::It: return kDictIt;
            case Language::Ja: return kDictJa;
            case Language::Ar: return kDictAr;
            case Language::En:
            default: return kDictEn;
            }
        }

    } // namespace

    const std::vector<LanguageInfo>& getAvailableLanguages() noexcept {
        return s_languages;
    }

    Language getCurrentLanguage() noexcept {
        return s_current_language;
    }

    std::string_view getCurrentLanguageCode() noexcept {
        for (const auto& info : s_languages) {
            if (info.lang == s_current_language) {
                return info.code;
            }
        }
        return "en";
    }

    std::string_view getCurrentLanguageName() noexcept {
        for (const auto& info : s_languages) {
            if (info.lang == s_current_language) {
                return info.name;
            }
        }
        return "English";
    }

    void setLanguage(Language lang) noexcept {
        s_current_language = lang;
    }

    bool setLanguageByCode(std::string_view code) noexcept {
        for (const auto& info : s_languages) {
            if (info.code == code) {
                s_current_language = info.lang;
                return true;
            }
        }
        return false;
    }

    void cycleLanguage(int dir) noexcept {
        int count = static_cast<int>(s_languages.size());
        if (count <= 0) {
            return;
        }
        int current_idx = 0;
        for (int i = 0; i < count; ++i) {
            if (s_languages[i].lang == s_current_language) {
                current_idx = i;
                break;
            }
        }
        int next_idx = (current_idx + dir) % count;
        if (next_idx < 0) {
            next_idx += count;
        }
        s_current_language = s_languages[next_idx].lang;
    }

    void initFromLocale() noexcept {
        const char* env = std::getenv("LC_ALL");
        if (!env || env[0] == '\0')
            env = std::getenv("LC_MESSAGES");
        if (!env || env[0] == '\0')
            env = std::getenv("LANG");

        if (!env || env[0] == '\0') {
            s_current_language = Language::En;
            return;
        }

        std::string loc(env);
        std::string code;
        for (char c : loc) {
            if (c == '_' || c == '.' || c == '@' || c == '-')
                break;
            code += static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
        }

        if (!setLanguageByCode(code)) {
            s_current_language = Language::En;
        }
    }

    std::string_view t(std::string_view key) noexcept {
        const auto& dict = getDictionary(s_current_language);
        auto it          = dict.find(key);
        if (it != dict.end()) {
            return it->second;
        }
        auto en_it = kDictEn.find(key);
        if (en_it != kDictEn.end()) {
            return en_it->second;
        }
        return key;
    }

} // namespace I18n
