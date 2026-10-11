"""The port's own lines in Italian, as tools/menu_languages.py writes them to
port/assets/menus/languages.xml (a line break a backslash and n, as in the
menus' files). Those a loc.map string says are its REFERENCES, not here,
but for the controls' names the edition's words for which are wider than
the port's controls screen has room for (it has two columns of keys).

Words the Italian edition has for the same things are its own: riapparizione
(respawn), uccisioni per vincere, the weapons' names (fucile for the
shotgun), PARTECIPA (join game), multigiocatore, Profilo:."""

# the English line: its Italian
LINES = {
    "%d players of %d, on %d machines%s": "%d di %d giocatori, su %d computer%s",
    "%s: %d %s of %d%s%s": "%s: %d %s su %d%s%s",
    "%s\\n%d of %d players\\non %d machines": "%s\\n%d di %d giocatori\\nsu %d computer",
    "%s\\n%s\\n%d of %d players\\n\\n%s": "%s\\n%s\\n%d di %d giocatori\\n\\n%s",
    "%s\\n%s\\nScore to win: %ld": "%s\\n%s\\nPunti per vincere: %ld",
    "%s\\n\\nThis game is under way.\\n\\n%s": "%s\\n\\nQuesta partita è in corso.\\n\\n%s",
    ", TEAMS": ", SQUADRE",
    ", full or starting": ", piena o in avvio",
    ", under way": ", in corso",
    ": under way": ": in corso",
    "A campaign level, played co-op": "Un livello della campagna, in cooperativa",
    "A hundred and fifty kills to win. Only a crowded\\nserver gets there.":
        "Centocinquanta uccisioni per vincere. Ci arriva\\nsolo un server affollato.",
    "A hundred kills to win. Made for big games.": "Cento uccisioni per vincere. Per partite grandi.",
    "A random primary weapon, of those the map has,\\neach time you spawn.":
        "Un'arma principale a caso, tra quelle della mappa,\\na ogni riapparizione.",
    "A random secondary weapon, of those the map has,\\neach time you spawn.":
        "Un'arma secondaria a caso, tra quelle della mappa,\\na ogni riapparizione.",
    "A team game's scoreboard: a column for each team,\\nor every player in order of score.":
        "Il punteggio a squadre: una colonna per squadra,\\no tutti i giocatori in ordine di punteggio.",
    "ADD PLAYER": "+ GIOCATORE",
    "AIM ASSIST:": "MIRA ASSISTITA:",
    "ALLIES": "ALLEATI",
    "ANYONE NEAR": "CHIUNQUE VICINO",
    "ARM SHIELDS:": "SCUDI SULLE BRACCIA:",
    "ASSAULT RIFLE": "FUCILE D'ASSALTO",
    "AUTO-CENTER LOOK:": "CENTRA VISUALE:",
    "Adjust the mouse's sensitivity\\nand aiming.\\n\\nProfile:":
        "Regola la sensibilità del mouse\\ne la mira.\\n\\nProfilo:",
    "Adjust the volume of the music\\nand of everything else.\\n\\nProfile:":
        "Regola il volume della musica\\ne di tutto il resto.\\n\\nProfilo:",
    "Another controller: START joins.": "Un altro controller: START per unirsi.",
    "Anyone can see and join your game: it is listed\\nin everyone's Server Browser.":
        "Chiunque può vedere la partita e unirsi: appare\\nnell'elenco server di tutti.",
    "BORDERLESS": "SENZA BORDI",
    "BUTTON LAYOUT:": "DISPOSIZIONE PULSANTI:",
    "BY SCORE": "PER PUNTEGGIO",
    "CATEGORY": "CATEGORIA",
    "CHECK FOR UPDATES:": "CERCA AGGIORNAMENTI:",
    "CLOSED": "CHIUSA",
    "CO-OP CAMPAIGN": "CAMPAGNA CO-OP",
    "CO-OP OPTIONS:": "OPZIONI CO-OP:",
    "CUSTOM MULTIPLAYER": "MULTIGIOCATORE PERSONALIZZATO",
    "CUSTOM SINGLEPLAYER": "CAMPAGNA PERSONALIZZATA",
    "Choose a window or the full\\nscreen, the frame rate and more.\\n\\nProfile:":
        "Scegli finestra o schermo intero,\\nfrequenza fotogrammi e altro.\\n\\nProfilo:",
    "Choose the keys and mouse\\nbuttons for each action.\\n\\nProfile:":
        "Scegli i tasti e i pulsanti\\ndel mouse per ogni azione.\\n\\nProfilo:",
    "Choose what to show, with left and right.": "Scegli cosa mostrare con sinistra e destra.",
    "Connecting to %s...": "Connessione a %s...",
    "Copy an invite link and PASTE LINK, or accept a Discord invite":
        "Copia un link d'invito e INCOLLA LINK, o accetta un invito Discord",
    "Could not join the game.": "Impossibile unirsi alla partita.",
    "Could not reach %s": "Impossibile raggiungere %s",
    "DIRECT LINK": "LINK DIRETTO",
    "DISPLAY MODE:": "MODALITÀ SCHERMO:",
    "DOWNLOAD MAPS:": "SCARICA MAPPE:",
    "Damage from the other players will only reduce\\nshields. Health will be unaffected.":
        "I danni degli altri giocatori ridurranno solo\\ngli scudi. La salute non ne risentirà.",
    "Downloading Custom Edition maps you do not have,\\nand helping send them to other players.":
        "Scaricare le mappe Custom Edition che non hai,\\ne aiutare a inviarle agli altri giocatori.",
    "Draw a frame for every display refresh, blending\\nbetween the game's 30 ticks a second.":
        "Un fotogramma a ogni aggiornamento dello schermo,\\nfondendo i 30 tick al secondo del gioco.",
    "Draw a panel behind the scoreboard, for clearer\\ntext.":
        "Un pannello dietro il punteggio, per un testo\\npiù leggibile.",
    "Draw first-person weapons, hands and attached\\nvisuals. Gameplay and sound continue when off.":
        "Disegna armi, mani ed effetti in prima persona.\\nGioco e suono continuano se disattivato.",
    "Draw text and titles with high-res fonts; off\\ndraws the game's own.":
        "Testi e titoli con caratteri ad alta risoluzione;\\nse disattivato, quelli del gioco.",
    "Draw the HUD from the high-res redraws; off\\ndraws the game's own pictures.":
        "L'HUD dai ridisegni ad alta risoluzione; se\\ndisattivato, le immagini del gioco.",
    "END GAME": "TERMINA PARTITA",
    "ENEMIES": "NEMICI",
    "EXTRA ENEMIES:": "NEMICI AGGIUNTIVI:",
    "Each enemy squad is this many times as large.": "Ogni squadra nemica è moltiplicata per questo valore.",
    "Each team's vehicles and their respawn time, for\\nthis game.":
        "I veicoli di ogni squadra e il loro tempo di\\nriapparizione, per questa partita.",
    "Echo sounds as the place you are in does, and\\nmuffle those behind walls, as the Xbox did.":
        "L'eco dei suoni come nel luogo in cui sei, e quelli\\ndietro i muri attutiti, come su Xbox.",
    "Enemy squads are as the campaign has them.": "Le squadre nemiche sono quelle della campagna.",
    "Enemy squads are the amount below times as large,\\nhowever many players there are.":
        "Le squadre nemiche sono moltiplicate per il valore\\nsotto, qualunque sia il numero di giocatori.",
    "Enemy squads grow with the players: by the amount\\nbelow for each player past the first.":
        "Le squadre nemiche crescono con i giocatori: del\\nvalore sotto per ogni giocatore oltre il primo.",
    "Enter sets the key chosen; left and right choose\\nwhich of two. Then press the new key or button.":
        "Invio imposta il tasto scelto; sinistra e destra\\nscelgono quale dei due. Poi premi tasto o pulsante.",
    "Everyone has a limitless supply of grenades, which\\nmakes for loud, explosive and occasionally messy fun.":
        "Tutti hanno granate illimitate, per un divertimento\\nrumoroso, esplosivo e a volte caotico.",
    "Everyone hears everyone in the lobby, before and\\nafter the game.":
        "Nella lobby tutti sentono tutti, prima e dopo\\nla partita.",
    "Everyone spawns with a assault rifle as their primary\\nweapon (where the map has one).":
        "Tutti riappaiono con un fucile d'assalto come arma\\nprincipale (se la mappa ne ha).",
    "Everyone spawns with a assault rifle as their secondary\\nweapon (where the map has one).":
        "Tutti riappaiono con un fucile d'assalto come arma\\nsecondaria (se la mappa ne ha).",
    "Everyone spawns with a needler as their primary\\nweapon (where the map has one).":
        "Tutti riappaiono con una pistola ad aghi come arma\\nprincipale (se la mappa ne ha).",
    "Everyone spawns with a needler as their secondary\\nweapon (where the map has one).":
        "Tutti riappaiono con una pistola ad aghi come arma\\nsecondaria (se la mappa ne ha).",
    "Everyone spawns with a pistol as their primary\\nweapon (where the map has one).":
        "Tutti riappaiono con una pistola come arma\\nprincipale (se la mappa ne ha).",
    "Everyone spawns with a pistol as their secondary\\nweapon (where the map has one).":
        "Tutti riappaiono con una pistola come arma\\nsecondaria (se la mappa ne ha).",
    "Everyone spawns with a plasma pistol as their primary\\nweapon (where the map has one).":
        "Tutti riappaiono con una pistola al plasma come arma\\nprincipale (se la mappa ne ha).",
    "Everyone spawns with a plasma pistol as their secondary\\nweapon (where the map has one).":
        "Tutti riappaiono con una pistola al plasma come arma\\nsecondaria (se la mappa ne ha).",
    "Everyone spawns with a plasma rifle as their primary\\nweapon (where the map has one).":
        "Tutti riappaiono con un fucile al plasma come arma\\nprincipale (se la mappa ne ha).",
    "Everyone spawns with a plasma rifle as their secondary\\nweapon (where the map has one).":
        "Tutti riappaiono con un fucile al plasma come arma\\nsecondaria (se la mappa ne ha).",
    "Everyone spawns with a rocket launcher as their primary\\nweapon (where the map has one).":
        "Tutti riappaiono con un lanciarazzi come arma\\nprincipale (se la mappa ne ha).",
    "Everyone spawns with a rocket launcher as their secondary\\nweapon (where the map has one).":
        "Tutti riappaiono con un lanciarazzi come arma\\nsecondaria (se la mappa ne ha).",
    "Everyone spawns with a shotgun as their primary\\nweapon (where the map has one).":
        "Tutti riappaiono con un fucile come arma\\nprincipale (se la mappa ne ha).",
    "Everyone spawns with a shotgun as their secondary\\nweapon (where the map has one).":
        "Tutti riappaiono con un fucile come arma\\nsecondaria (se la mappa ne ha).",
    "Everyone spawns with a sniper rifle as their primary\\nweapon (where the map has one).":
        "Tutti riappaiono con un fucile di precisione come\\narma principale (se la mappa ne ha).",
    "Everyone spawns with a sniper rifle as their secondary\\nweapon (where the map has one).":
        "Tutti riappaiono con un fucile di precisione come\\narma secondaria (se la mappa ne ha).",
    "Everyone starts with the primary and secondary\\nweapons below; the map's weapons are its own.":
        "Tutti iniziano con le armi principale e secondaria\\nsotto; le armi della mappa restano le sue.",
    "FAILED": "ERRORE",
    "FOV AND VIEWMODELS:": "FOV E MODELLI ARMA:",
    "FRAME RATE LIMIT:": "LIMITE FOTOGRAMMI:",
    "FULLSCREEN": "SCHERMO INTERO",
    "Five hundred kills to win. You'll be here a while.": "Cinquecento uccisioni per vincere. Ci vorrà un po'.",
    "Flying vehicles pitch as aircraft do: pushing up\\nflies down.":
        "I veicoli volanti beccheggiano come gli aerei:\\nspingendo su si scende.",
    "For each player past the first, enemy squads get\\nthis much more of themselves (100%: as many again).":
        "Per ogni giocatore oltre il primo, le squadre nemiche\\ncrescono di tanto (100%: il doppio).",
    "Free for all": "Tutti contro tutti",
    "Friendly fire and team balance, for this game.": "Fuoco amico e bilanciamento squadre, per questa partita.",
    "Friendly fire, the enemies and player collisions,\\nfor this game.":
        "Fuoco amico, nemici e collisioni tra giocatori,\\nper questa partita.",
    "Fullscreen takes the display; borderless covers it\\nwith a window. F11 switches to the window.":
        "Schermo intero occupa lo schermo; senza bordi lo\\ncopre con una finestra. F11 passa alla finestra.",
    "GAME NAME:": "NOME PARTITA:",
    "GAME TYPE:": "TIPO PARTITA:",
    "GLOW INTENSITY:": "INTENSITÀ BAGLIORE:",
    "GRAPHICS:": "GRAFICA:",
    "HELP SEND MAPS:": "AIUTA A INVIARE:",
    "HIGH-RES HUD:": "HUD ALTA RISOLUZIONE:",
    "HIGH-RES TEXT:": "TESTO ALTA RISOLUZIONE:",
    "Halo Custom\\nEdition map": "Mappa di Halo\\nCustom Edition",
    "Halo: Combat Evolved, the Xbox\\ngame, on this computer.\\n\\nProfile:":
        "Halo: Combat Evolved, il gioco\\nper Xbox, su questo computer.\\n\\nProfilo:",
    "Host a game on the Internet:\\nplayers join by its invite\\nlink or Discord.":
        "Ospita una partita su Internet:\\ni giocatori entrano col link\\nd'invito o con Discord.",
    "Host and join games over the internet by invite\\nlinks; off keeps to the local network.":
        "Ospita partite su Internet ed entra con i link\\nd'invito; se disattivato, solo la rete locale.",
    "How fast the right stick turns the view.": "Velocità della visuale con la levetta destra.",
    "How fast the view turns side to side for the\\nmouse's movement.":
        "Velocità con cui la visuale gira di lato al\\nmovimento del mouse.",
    "How fast the view turns up and down; Same\\nturns it as fast as side to side.":
        "Velocità con cui la visuale gira in alto e in basso;\\nUguale la gira come di lato.",
    "How large the players' names are drawn.": "La dimensione dei nomi dei giocatori.",
    "IN-GAME HELP:": "AIUTO IN GIOCO:",
    "INDICATOR OPTIONS:": "OPZIONI INDICATORE:",
    "INPUT DEVICE:": "DISPOSITIVO DI INGRESSO:",
    "INSTANT AIM:": "MIRA ISTANTANEA:",
    "INTERNET PLAY IS OFF (SETTINGS)": "INTERNET DISATTIVATO (IMPOSTAZIONI)",
    "INTERNET PLAY:": "GIOCO SU INTERNET:",
    "INVERT FLIGHT:": "INVERTI VOLO:",
    "INVERT LOOK:": "INVERTI VISUALE:",
    "INVITE LINK:": "LINK D'INVITO:",
    "ITEM OPTIONS:": "OPZIONI OGGETTI:",
    "In another's game, send its map to players who\\njoin without it, as a torrent's peers do.":
        "Nella partita di un altro, invia la sua mappa a chi\\nentra senza, come fanno i peer di un torrent.",
    "In first person, turn the view the moment the\\nmouse moves, not up to two ticks later.":
        "In prima persona, gira la visuale appena il mouse\\nsi muove, non fino a due tick dopo.",
    "In multiplayer, whose names are drawn above their\\nheads.":
        "In multigiocatore, di chi mostrare il nome sopra\\nla testa.",
    "Internet play is off (Settings)": "Il gioco su Internet è disattivato (Impostazioni)",
    "Internet play, updates and the\\nmultiplayer HUD.\\n\\nProfile:":
        "Gioco su Internet, aggiornamenti\\ne HUD multigiocatore.\\n\\nProfilo:",
    "It cannot be joined now:\\nit is loading, over or full.":
        "Ora non è possibile entrare:\\nsta caricando, è finita o è piena.",
    "JOIN FROM CLIPBOARD:": "ENTRA DAGLI APPUNTI:",
    "JOIN GAME joins it now;\\nits players show then.": "PARTECIPA per entrare ora;\\npoi appariranno i giocatori.",
    "JOINING THE GAME\\nYou spawn beside a teammate once it is safe":
        "INGRESSO IN PARTITA\\nRiapparirai accanto a un compagno appena sarà sicuro",
    "Join a game by its invite link,\\nor one a Discord invite\\nreached.":
        "Entra in una partita col suo\\nlink d'invito o con un invito\\nDiscord.",
    "Join the game of an invite link copied before\\nswitching to the game.":
        "Entra nella partita di un link d'invito copiato\\nprima di passare al gioco.",
    "Joining a game on a Custom Edition map you do not\\nhave: download it from the players (if shared).":
        "Entrando in una partita su una mappa Custom Edition\\nche non hai: scaricala dai giocatori (se condivisa).",
    "LEAVE": "ESCI",
    "LEGACY": "CLASSICA",
    "LEGACY SOUTHPAW": "CLASSICA MANCINA",
    "LEGS:": "GAMBE:",
    "LIGHT SHOW": "SPETTACOLO DI LUCI",
    "LISTING:": "VISIBILITÀ:",
    "LIVE": "IN CORSO",
    "LOADOUT:": "DOTAZIONE:",
    "LOBBY VOICE CHAT:": "CHAT VOCALE LOBBY:",
    "LOOK SENSITIVITY:": "SENSIBILITÀ VISUALE:",
    "Let internet play ask the router to forward its\\nport, for networks that stop connections.":
        "Il gioco su Internet chiede al router di inoltrare\\nla porta, per le reti che bloccano le connessioni.",
    "Level the view while walking.": "Livella la visuale mentre cammini.",
    "Light Show: the shield glow three times as far,\\nat full from a third of the flare.":
        "Spettacolo di luci: bagliore dello scudo tre volte\\npiù ampio, pieno da un terzo del lampo.",
    "Light models for each pixel, without the facets\\nof the Xbox's lighting for each vertex.":
        "Illumina i modelli per pixel, senza le sfaccettature\\ndell'illuminazione per vertice di Xbox.",
    "Lives, health, shields and respawning, for this game.": "Vite, salute, scudi e riapparizione, per questa partita.",
    "Look for a new version when the game starts.": "Cerca una nuova versione all'avvio del gioco.",
    "Looking for games on your LAN...": "Ricerca partite sulla tua LAN...",
    "Looking for public games...": "Ricerca partite pubbliche...",
    "MADE WHEN THE GAME STARTS": "CREATO ALL'AVVIO DELLA PARTITA",
    "MAP SHARING": "CONDIVISIONE MAPPE",
    "MAP SHARING:": "CONDIVISIONE MAPPE:",
    "MAP WEAPONS:": "ARMI DELLA MAPPA:",
    "MAXIMUM PLAYERS:": "GIOCATORI MAX:",
    "MULTIPLIER:": "MOLTIPLICATORE:",
    "Moving the mouse forward looks down.": "Spostando il mouse in avanti si guarda in basso.",
    "NATIVE": "NATIVA",
    "NEEDLER": "PISTOLA AD AGHI",
    "NO GRENADES": "NIENTE GRANATE",
    "NONE: A LAN GAME": "NESSUNO: PARTITA IN LAN",
    "Native draws at the resolution; Original draws\\nthe Xbox's 640x480 and scales it up.":
        "Nativa disegna alla risoluzione; Originale disegna\\na 640x480 come Xbox e ingrandisce.",
    "New player: press START.": "Nuovo giocatore: premi START.",
    "No primary weapon.": "Nessuna arma principale.",
    "No public games found": "Nessuna partita pubblica trovata",
    "No secondary weapon.": "Nessuna arma secondaria.",
    "No voice chat during the game.": "Nessuna chat vocale durante la partita.",
    "No voice chat in the lobby.": "Nessuna chat vocale nella lobby.",
    "No weapons appear on the map: with a loadout of\\nNONE and NONE, melee and grenades only.":
        "Nessuna arma nella mappa: con dotazione NESSUNO\\ne NESSUNO, solo corpo a corpo e granate.",
    "OPEN MIC": "MICROFONO APERTO",
    "ORIGINAL": "ORIGINALE",
    "OUTPUT DEVICE:": "DISPOSITIVO DI USCITA:",
    "On foot, horizontal at 16:9, in degrees.\\nDefault keeps the stock view.":
        "A piedi, orizzontale in 16:9, in gradi.\\nPredefinito mantiene la visuale originale.",
    "Only players with your invite link can join.": "Solo chi ha il tuo link d'invito può entrare.",
    "PASTE LINK": "INCOLLA LINK",
    "PAUSE MENU": "MENU PAUSA",
    "PER PLAYER": "PER GIOCATORE",
    "PER PLAYER:": "PER GIOCATORE:",
    "PER-PIXEL LIGHTING:": "ILLUMINAZIONE PER PIXEL:",
    "PISTOL": "PISTOLA",
    "PLASMA PISTOL": "PISTOLA AL PLASMA",
    "PLASMA RIFLE": "FUCILE AL PLASMA",
    "PLAYER COLLISIONS:": "COLLISIONI GIOCATORI:",
    "PLAYER NAME SIZE:": "DIMENSIONE NOMI:",
    "PLAYER NAMES:": "NOMI GIOCATORI:",
    "PLAYER OPTIONS:": "OPZIONI GIOCATORI:",
    "PRESS A KEY": "PREMI UN TASTO",
    "PRESS SPACE OR A TO VOTE TO SKIP   %d OF %d": "PREMI SPAZIO O A PER VOTARE IL SALTO   %d SU %d",
    "PRIMARY WEAPON:": "ARMA PRINCIPALE:",
    "PRIVATE": "PRIVATA",
    "PUBLIC": "PUBBLICA",
    "PUSH TO TALK": "PREMI E PARLA",
    "Play sound at all; from the next time the game\\nstarts.": "Riproduce l'audio; dal prossimo avvio del\\ngioco.",
    "Play the campaign with a\\nfriend in split screen:\\nplayer 2 on a gamepad.":
        "Gioca la campagna con un\\namico a schermo diviso:\\ngiocatore 2 col gamepad.",
    "Player 2 chooses their\\nprofile with their own\\ncontroller.":
        "Il giocatore 2 sceglie\\nil suo profilo con il\\nproprio controller.",
    "Player 2: choose with your controller.": "Giocatore 2: scegli con il tuo controller.",
    "Players 15 metres apart or less are near\\nfor voice chat.":
        "I giocatori a 15 metri o meno sono vicini\\nper la chat vocale.",
    "Players 150 metres apart or less are near\\nfor voice chat.":
        "I giocatori a 150 metri o meno sono vicini\\nper la chat vocale.",
    "Players 30 metres apart or less are near\\nfor voice chat.":
        "I giocatori a 30 metri o meno sono vicini\\nper la chat vocale.",
    "Players 45 metres apart or less are near\\nfor voice chat.":
        "I giocatori a 45 metri o meno sono vicini\\nper la chat vocale.",
    "Players 60 metres apart or less are near\\nfor voice chat.":
        "I giocatori a 60 metri o meno sono vicini\\nper la chat vocale.",
    "Players 90 metres apart or less are near\\nfor voice chat.":
        "I giocatori a 90 metri o meno sono vicini\\nper la chat vocale.",
    "Players bump into each other, as in the campaign.": "I giocatori si urtano, come nella campagna.",
    "Players can be hurt by damage from explosives\\nfired by the other players.":
        "I giocatori possono subire danni dagli esplosivi\\nutilizzati dagli altri giocatori.",
    "Players can be hurt by weapons or explosives\\nfired by the other players.":
        "I giocatori possono essere colpiti da armi ed esplosivi\\nutilizzati dagli altri giocatori.",
    "Players can not be hurt by weapons and explosives\\nfired by the other players.":
        "I giocatori non possono essere colpiti da armi ed esplosivi\\nutilizzati dagli altri giocatori.",
    "Players can vote to kick a player, from the\\nscoreboard.":
        "I giocatori possono votare per espellere un\\ngiocatore, dal punteggio.",
    "Players cannot vote to kick anyone.": "I giocatori non possono votare espulsioni.",
    "Players from the Server Browser must type it to\\njoin (an invite link needs none). Enter sets it.":
        "Chi arriva dall'elenco server deve digitarla per\\nentrare (col link d'invito no). Invio la imposta.",
    "Players hear all their teammates, and the enemies\\nwho are near them.":
        "I giocatori sentono tutti i compagni di squadra e\\ni nemici vicini.",
    "Players hear all their teammates, wherever they\\nare.":
        "I giocatori sentono tutti i compagni di squadra,\\novunque siano.",
    "Players hear everyone who is near them.": "I giocatori sentono chiunque sia vicino.",
    "Players hear their teammates who are near them.": "I giocatori sentono i compagni di squadra vicini.",
    "Players join this game by its invite link, or your\\nDiscord invite. Enter copies the link again.":
        "Si entra in questa partita col link d'invito o il tuo\\ninvito Discord. Invio copia di nuovo il link.",
    "Players walk through each other, so that no one\\nblocks a doorway. The AI's characters still block.":
        "I giocatori si attraversano, così nessuno blocca\\nuna porta. I personaggi dell'IA bloccano ancora.",
    "Players who join without the map cannot play.": "Chi entra senza la mappa non può giocare.",
    "Players who join without the map download it\\nfrom you and the other players.":
        "Chi entra senza la mappa la scarica\\nda te e dagli altri giocatori.",
    "Press a key, a mouse button or turn the wheel...\\nEscape cancels, Delete clears it.":
        "Premi un tasto, un pulsante del mouse o gira la\\nrotella... Esc annulla, Canc lo cancella.",
    "Pushing the right stick up looks down.": "Spingendo in su la levetta destra si guarda in basso.",
    "RANDOM": "CASUALE",
    "RESOLUTION SCALING:": "SCALA RISOLUZIONE:",
    "REVERB:": "RIVERBERO:",
    "ROCKET LAUNCHER": "LANCIARAZZI",
    "Rumble the controller.": "Fa vibrare il controller.",
    "SAME": "UGUALE",
    "SCOREBOARD LAYOUT:": "DISPOSIZIONE PUNTEGGI:",
    "SCOREBOARD PANEL:": "PANNELLO PUNTEGGI:",
    "SECONDARY WEAPON:": "ARMA SECONDARIA:",
    "SERVER BROWSER": "ELENCO SERVER",
    "SETTINGS": "IMPOSTAZIONI",
    "SHADOW RESOLUTION:": "RISOLUZIONE OMBRE:",
    "SHARE MAPS:": "CONDIVIDI MAPPE:",
    "SHIELD COLOR:": "COLORE SCUDO:",
    "SHIELD GLOW:": "BAGLIORE SCUDO:",
    "SHOTGUN": "FUCILE",
    "SHOW:": "MOSTRA:",
    "SINGLEPLAYER": "GIOCATORE SINGOLO",
    "SMOOTH MOTION:": "MOVIMENTO FLUIDO:",
    "SNIPER RIFLE": "FUCILE DI PRECISIONE",
    "SOUND:": "AUDIO:",
    "SOUTHPAW": "MANCINA",
    "SPECTATING %.12s   (A: NEXT)\\n%s": "OSSERVI %.12s   (A: SUCCESSIVO)\\n%s",
    "START NOW": "INIZIA ORA",
    "STATIC MULTIPLIER": "MOLTIPLICATORE FISSO",
    "STICK LAYOUT:": "DISPOSIZIONE LEVETTE:",
    "SWAP A, L TRIGGER": "SCAMBIA A, GRILLETTO SX",
    "SWAP B, L TRIGGER": "SCAMBIA B, GRILLETTO SX",
    "SWAP B, R STICK": "SCAMBIA B, LEVETTA DX",
    "SWAP TRIGGERS": "SCAMBIA GRILLETTI",
    "SWITCH TEAM": "CAMBIA SQUADRA",
    "SYSTEM DEFAULT": "PREDEFINITO",
    "Sending players who join without the game's\\nCustom Edition map the map, and how fast.":
        "Inviare la mappa Custom Edition della partita\\na chi entra senza, e a che velocità.",
    "Set this profile's controller\\nsensitivity and layouts.\\n\\nProfile:":
        "Imposta sensibilità e disposizione\\ndel controller di questo profilo.\\n\\nProfilo:",
    "Seventy-five kills to win. Settle in for a long\\nfight.":
        "Settantacinque uccisioni per vincere. Preparati a\\nuna lunga battaglia.",
    "Shields light their surroundings as they flare,\\nin the shield's own color.":
        "Gli scudi illuminano ciò che li circonda quando\\nbrillano, nel colore dello scudo.",
    "Show the game's hints about its controls.": "Mostra i suggerimenti del gioco sui controlli.",
    "Slow and drag the view along with a target while\\naiming with the mouse, as with a controller.":
        "Rallenta e trascina la visuale col bersaglio mentre\\nmiri col mouse, come con un controller.",
    "Smooth jagged edges, which the Xbox did not. FXAA\\nand SMAA are cheap; SSAA and MSAA are sharper.":
        "Smussa i bordi seghettati, cosa che Xbox non faceva.\\nFXAA e SMAA sono leggeri; SSAA e MSAA più nitidi.",
    "Smooth jagged edges, which the Xbox did not. FXAA\\nis cheap; MSAA is sharper.":
        "Smussa i bordi seghettati, cosa che Xbox non faceva.\\nFXAA è leggero; MSAA è più nitido.",
    "Starting in:": "Inizio tra:",
    "TEAM": "SQUADRA",
    "TEAM NEAR": "SQUADRA VICINA",
    "TEAM, ENEMIES NEAR": "SQUADRA E NEMICI",
    "TEAMPLAY OPTIONS:": "OPZIONI SQUADRE:",
    "TEAMS": "SQUADRE",
    "Talk in network games: while PUSH TO TALK is held\\n(Controls), whenever you speak, or never.":
        "Parla nelle partite in rete: tenendo PREMI PER PARLARE\\n(Controlli), ogni volta che parli, o mai.",
    "Teams": "Squadre",
    "That is not the game's password. Try again.": "Non è la password della partita. Riprova.",
    "The HUD, text, anti-aliasing, shadows and\\nlighting.": "HUD, testo, anti-aliasing, ombre e\\nilluminazione.",
    "The energy shield's flare on your first-person\\narms too, as on your body.":
        "Il lampo dello scudo energetico anche sulle braccia\\nin prima persona, come sul corpo.",
    "The field of view and the first-person\\nweapon. Their defaults keep the stock view.":
        "Il campo visivo e l'arma in prima persona.\\nI predefiniti mantengono la visuale originale.",
    "The game is full.": "La partita è piena.",
    "The game is gone.": "La partita non c'è più.",
    "The gametype and its rules, for this game.": "Il tipo di partita e le sue regole, per questa partita.",
    "The map's weapons appear where its designers\\nplaced them.":
        "Le armi della mappa appaiono dove i suoi autori\\nle hanno messe.",
    "The map's weapons, without any grenades.": "Le armi della mappa, senza granate.",
    "The microphone voice chat listens to.": "Il microfono usato dalla chat vocale.",
    "The most players the game takes (up to 128).": "Il massimo di giocatori della partita (fino a 128).",
    "The most you send of maps to other players, so\\nthat the game keeps the rest of your upload.":
        "Il massimo che invii di mappe agli altri giocatori,\\nperché il gioco tenga il resto del tuo upload.",
    "The most you send of maps, so that the game\\nkeeps the rest of your upload.":
        "Il massimo che invii di mappe, perché il gioco\\ntenga il resto del tuo upload.",
    "The motion tracker and nav points, for this game.":
        "Rilevatore di movimento e punti di navigazione,\\nper questa partita.",
    "The music's volume.": "Il volume della musica.",
    "The name the game shows in the lists of games.\\nEnter changes it.":
        "Il nome della partita negli elenchi delle partite.\\nInvio lo cambia.",
    "The new player's profile.": "Il profilo del nuovo giocatore.",
    "The other players' voices.": "Le voci degli altri giocatori.",
    "The server browser is off (network.public_lobby)": "L'elenco server è disattivato (network.public_lobby)",
    "The size objects' shadows are drawn at: 128 as on\\nthe Xbox; larger for smoother, as soft, edges.":
        "La risoluzione delle ombre degli oggetti: 128 come\\nsu Xbox; di più per bordi più lisci, ugualmente morbidi.",
    "The volume of every other sound: effects and\\nspeech.": "Il volume di ogni altro suono: effetti e\\ndialoghi.",
    "The volume of everything.": "Il volume di tutto.",
    "The weapon and hands, horizontal at 16:9.\\nDefault keeps the weapon's stock view.":
        "L'arma e le mani, orizzontale in 16:9.\\nPredefinito mantiene la visuale originale dell'arma.",
    "The weapons follow the weapon set above.": "Le armi seguono il set di armi sopra.",
    "The window's size: 4:3, then 16:10, 16:9 and 21:9\\n(its edges can also be dragged).":
        "La dimensione della finestra: 4:3, poi 16:10, 16:9 e\\n21:9 (si possono anche trascinare i bordi).",
    "This game has a password. Type it, then press\\nEnter to join.":
        "Questa partita ha una password. Digitala, poi\\npremi Invio per entrare.",
    "Two hundred and fifty kills to win.": "Duecentocinquanta uccisioni per vincere.",
    "Two hundred kills to win. Bring friends. Lots of\\nthem.":
        "Duecento uccisioni per vincere. Porta degli amici.\\nTanti amici.",
    "UPLOAD LIMIT:": "LIMITE DI UPLOAD:",
    "UPNP PORT FORWARDING:": "INOLTRO PORTE UPNP:",
    "VEHICLE OPTIONS:": "OPZIONI VEICOLO:",
    "VIBRATION:": "VIBRAZIONE:",
    "VIEWMODEL FOV:": "FOV ARMA:",
    "VIEWMODELS:": "MODELLI ARMA:",
    "VOICE AND VOTING:": "VOCE E VOTAZIONI:",
    "VOICE CHAT:": "CHAT VOCALE:",
    "VOICE NEAR DISTANCE:": "DISTANZA VOCE VICINA:",
    "VOICE QUALITY:": "QUALITÀ VOCE:",
    "VOICE VOLUME:": "VOLUME VOCE:",
    "VOTE KICK:": "VOTO ESPULSIONE:",
    "VOTED TO SKIP   %d OF %d": "VOTATO IL SALTO   %d SU %d",
    "Voice chat and votes to kick a player, for this\\ngame.":
        "Chat vocale e voti per espellere un giocatore, per\\nquesta partita.",
    "Voices at 12 kilobits a second, in the lobby\\nand in the game.":
        "Voci a 12 kilobit al secondo, nella lobby\\ne in partita.",
    "Voices at 16 kilobits a second, in the lobby\\nand in the game.":
        "Voci a 16 kilobit al secondo, nella lobby\\ne in partita.",
    "Voices at 24 kilobits a second, in the lobby\\nand in the game.":
        "Voci a 24 kilobit al secondo, nella lobby\\ne in partita.",
    "Voices at 32 kilobits a second, in the lobby\\nand in the game.":
        "Voci a 32 kilobit al secondo, nella lobby\\ne in partita.",
    "Voices at 48 kilobits a second, in the lobby\\nand in the game.":
        "Voci a 48 kilobit al secondo, nella lobby\\ne in partita.",
    "Voices at 64 kilobits a second, in the lobby\\nand in the game.":
        "Voci a 64 kilobit al secondo, nella lobby\\ne in partita.",
    "Voices at 8 kilobits a second, in the lobby\\nand in the game.":
        "Voci a 8 kilobit al secondo, nella lobby\\ne in partita.",
    "WEAPONS": "ARMI",
    "WINDOW SIZE:": "DIMENSIONE FINESTRA:",
    "WINDOWED": "FINESTRA",
    "Wait for the display between frames, so that the\\npicture never tears.":
        "Attende lo schermo tra i fotogrammi, perché\\nl'immagine non si strappi mai.",
    "Waiting for players": "In attesa di giocatori",
    "Weapons, grenades and starting equipment, for\\nthis game.":
        "Armi, granate e dotazione iniziale, per\\nquesta partita.",
    "What fullscreen and borderless draw at. Fullscreen\\nsets the display to it; borderless scales it.":
        "La risoluzione di schermo intero e senza bordi.\\nSchermo intero la imposta; senza bordi la scala.",
    "Where the game's sound and the voices play.": "Dove si sentono l'audio del gioco e le voci.",
    "Which of the controller's buttons does what (not\\nthe keyboard's: Controls Setup sets those).":
        "Cosa fa ogni pulsante del controller (non quelli\\ndella tastiera: li imposta Impostazioni controlli).",
    "Which stick moves and which looks.": "Quale levetta muove e quale guarda.",
    "With V-Sync off, the most frames a second. Auto:\\ntwice the display's refresh rate.":
        "Senza V-Sync, il massimo di fotogrammi al secondo.\\nAuto: il doppio della frequenza dello schermo.",
    "You come back beside them once it is safe": "Tornerai al suo fianco appena sarà sicuro",
    "You join them when they're on foot": "Ti unirai quando sarà a piedi",
    "Your legs in first person, seen looking down,\\nmoving as you run. Your body as others see it.":
        "Le tue gambe in prima persona, guardando in basso,\\nmentre corri. Il tuo corpo come lo vedono gli altri.",
    "Your shield's flare in an armor color, whatever\\nyour armor. Only you see it. Default: its own.":
        "Il lampo dello scudo in un colore armatura, qualunque\\nsia l'armatura. Lo vedi solo tu. Predefinito: il suo.",
    "\\n\\nInvite link copied:\\npaste it to friends": "\\n\\nLink d'invito copiato:\\nincollalo agli amici",
    "\\n\\nJoining from here:": "\\n\\nEntra da qui:",
    "player": "giocatore",
    "players": "giocatori",
    # (the controls' keys and buttons no loc.map string says)
    "Keypad Comma": "TASTIERINO ,",
    "Wheel Up": "RUOTA SU",
    "Wheel Down": "RUOTA GIÙ",
    # (the controls and keys the loc.map says in words too wide for the
    # port's columns: "SPOSTATI IN AVANTI", "BARRA SPAZIATRICE")
    "MOVE FORWARD": "AVANZA",
    "MOVE BACKWARD": "INDIETREGGIA",
    "STRAFE LEFT": "PASSO A SINISTRA",
    "STRAFE RIGHT": "PASSO A DESTRA",
    "MELEE": "CORPO A CORPO",
    "START GAME": "INIZIA PARTITA",
    # (the port's titles)
    "PLAYER 2 PROFILE": "PROFILO GIOCATORE 2",
    "GAME LOBBY": "LOBBY PARTITA",
    "CO-OP OPTIONS": "OPZIONI COOPERATIVA",
    "VOICE AND VOTING": "VOCE E VOTAZIONI",
    "Left Shift": "MAIUSC SX",
    "Right Shift": "MAIUSC DX",
    "Left Ctrl": "CTRL SX",
    "Right Ctrl": "CTRL DX",
    "Left Alt": "ALT SX",
    "Right Alt": "ALT DX",
    "Left GUI": "WINDOWS SX",
    "Right GUI": "WINDOWS DX",
    "Space": "SPAZIO",
    "Left": "FRECCIA SX",
    "Right": "FRECCIA DX",
    "Keypad Enter": "TAST. INVIO",
    "Mouse Left": "PULSANTE SX",
    "Mouse Middle": "CLIC RUOTA",
    "Mouse Right": "PULSANTE DX",
}

# lines Italian says as English does
SAME = {
    "12 KBPS", "16 KBPS", "24 KBPS", "32 KBPS", "48 KBPS", "64 KBPS", "8 KBPS", "1 MB/S", "128 KB/S", "2 MB/S",
    "256 KB/S", "4 MB/S", "512 KB/S", "64 KB/S", "8 MB/S", "ANTI-ALIASING:", "AUTO", "CO-OP", "FOV:", "FXAA", "MSAA 2X",
    "MSAA 4X", "MSAA 8X", "PASSWORD", "PASSWORD:", "SMAA", "SSAA 2X", "V-SYNC:", ", password",
}
