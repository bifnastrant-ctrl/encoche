# Encoche

Une « notch » pour Windows 10/11 : une pastille noire collée en haut de l'écran qui
s'ouvre au survol. MVP en C++ (Win32 + GDI+ + C++/WinRT).

## Ce que ça fait

- Notch animée, centrée en haut de l'écran principal, compatible DPI.
- Page **Musique** : titre, artiste, pochette, précédent / lecture-pause / suivant, **barre de
  progression** (clic pour se déplacer dans le morceau) et **paroles synchronisées** (LRCLIB,
  sans clé d'API). Fonctionne avec Spotify, YouTube, Apple Music... via le contrôle média de Windows.
- Page **Prière** : horaires de ta mosquée via **Mawaqit**, prochaine prière + compte à rebours.
  Dans le notch fermé, affiche la prochaine prière (compte à rebours dans les 30 dernières minutes).
- Page **Horloge** (12 h / 24 h).
- **Engrenage** en haut à droite quand le notch est ouvert : couleur d'accent, arrondi des coins,
  paroles on/off, heure 24 h, lancement avec Windows, choix de la mosquée.
- **Icône dans la zone de notification** (clic gauche : réglages, clic droit : menu) et
  **démarrage avec Windows** (case dans les réglages ou le menu, ou tâche de l'installateur).
- Molette sur la notch ouverte : changer de page. Se cache devant les applications plein écran.
- Les réglages sont enregistrés dans `%APPDATA%\Encoche\settings.ini`.

## Configurer la mosquée (Mawaqit)

Ouvre la page de ta mosquée sur mawaqit.net, copie l'adresse (par ex.
`https://mawaqit.net/fr/nom-de-la-mosquee`), puis dans les réglages clique sur **Mosquée** et colle-la.
Mawaqit n'a pas d'API publique : Encoche lit `confData` dans la page de la mosquée (même méthode que l'API non officielle mrsofiane/mawaqit-api).
Si Mawaqit change son format, seule `ParseConfData()` dans `prayer_module.h` est à adapter.

## Compiler

Prérequis : Visual Studio 2022 avec la charge de travail « Développement Desktop en C++ »
(inclut le SDK Windows et C++/WinRT) et CMake.

```
cmake -B build -G "Visual Studio 17 2022" -A x64
cmake --build build --config Release
build\Release\Encoche.exe
```

## Ajouter une application (module)

1. Créer une classe qui hérite de `INotchModule` (voir `clock_module.h`, le plus simple).
2. Implémenter `Tick()` (true = redessiner), `DrawCompact`, `DrawExpanded`, `OnClick`.
3. L'ajouter dans `main.cpp` : `app.modules.push_back(std::make_unique<MonModule>());`

## Prochaines étapes

- Notifications / sons à l'heure de la prière (adhan), iqama.
- Étagère à fichiers, minuteur Pomodoro, rappel d'eau (écartés pour l'instant).

## Obtenir un .exe / un installateur sans rien installer

1. Créez un dépôt GitHub et poussez-y le contenu de ce dossier (la racine du dépôt
   doit contenir `CMakeLists.txt`, `installer.iss`, `src/` et `.github/`).
2. Onglet **Actions** > workflow **build** > **Run workflow** (ou simple push sur `main`).
3. Quand c'est vert, téléchargez l'artefact **Encoche-Setup** (`EncocheSetup.exe`)
   ou **Encoche-exe** (l'exécutable seul).

L'installateur s'installe sans droits administrateur, ajoute un raccourci dans le menu
Démarrer et propose de lancer Encoche au démarrage de Windows.
Comme il n'est pas signé, Windows SmartScreen affichera un avertissement
(« Informations complémentaires » puis « Exécuter quand même »).
