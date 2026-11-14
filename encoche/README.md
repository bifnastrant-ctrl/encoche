# Encoche

Une « notch » pour Windows 10/11 : une pastille noire collée en haut de l'écran qui
s'ouvre au survol. MVP en C++ (Win32 + GDI+ + C++/WinRT).

## Ce que fait le MVP

- Notch animée, centrée en haut de l'écran principal, compatible DPI.
- Page **Musique** : titre, artiste, pochette, précédent / lecture-pause / suivant.
  Fonctionne avec Spotify, YouTube (navigateur), Apple Music... via le contrôle média
  de Windows, sans clé d'API.
- Page **Horloge** (exemple de module).
- Molette de la souris sur la notch ouverte : changer de page.
- Clic droit : menu « Quitter ».
- Se cache devant les applications plein écran.
- Au repos : un timer de 100 ms qui ne dessine rien tant que rien ne change.

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

- Module **Prière** : horaires via l'API Aladhan (gratuite) ; Mawaqit à vérifier.
- Paroles synchronisées via LRCLIB.
- Barre de progression du morceau.
- Icône dans la zone de notification, démarrage avec Windows.
- Étagère à fichiers (glisser-déposer), minuteur Pomodoro, rappel d'eau.

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
