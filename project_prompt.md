# Aufgabe: Verteiltes ROS-2-Pick-and-Place-System auf ThinkPad + Jetson Nano aufsetzen

Du arbeitest auf meinem **ThinkPad T440 mit Ubuntu 24.04 LTS** in VS Code.

Zusätzlich befindet sich in meinem lokalen Netzwerk ein **NVIDIA Jetson Nano**, der per

```bash
ssh jetson
```

erreichbar ist.

Der Jetson hat:

```text
Ubuntu 18.04.6 LTS
L4T R32.7.6
Kernel 4.9.337-tegra
Architecture: aarch64
Docker 20.10.21
```

## Zielarchitektur

Ich möchte ein kleines, aber technisch sauberes verteiltes ROS-2-System aufbauen.

### ThinkPad T440

Ubuntu 24.04 LTS.

Hier sollen laufen:

```text
ROS 2 Jazzy
Gazebo Harmonic
Gazebo GUI
ros_gz
gz_ros2_control
ros2_control
robot_state_publisher
simulierter Franka Panda
simulierte Umgebung
```

Der ThinkPad soll **Simulation, Physics, Rendering und die simulierten Aktoren** übernehmen.

### Jetson Nano

Der bestehende Ubuntu-18.04-/JetPack-/L4T-Host darf NICHT auf eine neue Ubuntu-Version aktualisiert oder neu geflasht werden.

ROS 2 soll ausschließlich in einem ARM64-Docker-Container laufen.

Bevorzugte Basis:

```text
ros:jazzy-ros-base-noble
```

Im Container sollen laufen:

```text
ROS 2 Jazzy
MoveIt 2
move_group
Motion Planning / OMPL
Pick-and-Place State Machine
Planning Scene
IK
Trajectory Generation
```

Der Jetson soll **keine GUI und kein Gazebo** ausführen.

MoveIt soll möglichst als Binary-Paket über apt installiert werden. MoveIt nicht unnötig komplett aus Source bauen, insbesondere nicht auf dem Jetson Nano.

Keine NVIDIA-GPU-Unterstützung im Container konfigurieren, solange sie für dieses Beispiel nicht benötigt wird. Motion Planning kann CPU-only laufen.

---

# Gewünschter Datenfluss

Die Architektur soll tatsächlich verteilt sein:

```text
ThinkPad / Gazebo
       │
       │ /joint_states
       │ TF
       │ controller states
       ▼
Jetson Nano
       │
       │ MoveIt 2
       │ IK
       │ collision checking
       │ OMPL planning
       │ trajectory generation
       ▼
FollowJointTrajectory
       │
       ▼
ThinkPad / ros2_control
       │
       ▼
Gazebo Panda
```

Es ist NICHT akzeptabel, MoveIt oder die Pick-and-Place-Berechnung heimlich auf dem ThinkPad laufen zu lassen.

Die Trajektorienplanung muss auf dem Jetson erfolgen.

Gazebo muss auf dem ThinkPad laufen.

---

# ROS-2-Netzwerk

Verwende auf beiden Maschinen:

```bash
ROS_DOMAIN_ID=42
```

Bevorzugt soll auf beiden Maschinen dieselbe RMW-Implementation verwendet werden:

```text
Cyclone DDS
rmw_cyclonedds_cpp
```

Konfiguriere den Jetson-Docker-Container mit:

```text
--network host
```

damit ROS-2-/DDS-Discovery nicht durch Docker NAT behindert wird.

Verwende kein `--privileged`, sofern es nicht technisch notwendig ist.

Verändere Firewall-Regeln nicht blind.

Zuerst normale Multicast-Discovery testen.

Nur wenn DDS-Discovery über das LAN nicht zuverlässig funktioniert, eine explizite Cyclone-DDS-Konfiguration mit den realen Netzwerkinterfaces bzw. Peer-Adressen erzeugen.

Keine IP-Adressen hart codieren, bevor du sie nicht selbst auf beiden Hosts ermittelt hast.

---

# WICHTIG: Erst analysieren, dann installieren

Beginne NICHT sofort mit `apt install`.

Führe zuerst eine Bestandsaufnahme durch.

Lokal auf dem ThinkPad mindestens:

```bash
cat /etc/os-release
uname -a
ip -br addr
which ros2 || true
printenv | grep -E 'ROS|RMW|CYCLONE' || true
dpkg -l | grep -E 'ros-|gazebo|gz-' || true
```

Remote auf dem Jetson mindestens:

```bash
ssh jetson 'cat /etc/os-release'
ssh jetson 'uname -a'
ssh jetson 'ip -br addr'
ssh jetson 'docker --version'
ssh jetson 'docker info'
ssh jetson 'df -h'
ssh jetson 'free -h'
```

Prüfe außerdem, ob Docker ohne sudo funktioniert.

Falls nicht, verwende nötigenfalls `sudo docker`, aber ändere nicht einfach Benutzergruppen oder Sicherheitskonfigurationen.

Falls ein interaktives sudo-Passwort erforderlich ist, versuche NICHT, Sicherheitsmechanismen zu umgehen. Teile mir stattdessen exakt mit, welchen einzelnen Befehl ich manuell ausführen muss.

---

# Keine unnötigen Änderungen am Jetson Host

Auf dem Jetson NICHT:

```text
Ubuntu upgraden
JetPack upgraden
L4T upgraden
Kernel ändern
ROS direkt auf Ubuntu 18.04 installieren
Gazebo installieren
Docker aktualisieren, solange nicht zwingend erforderlich
NVIDIA-Treiber verändern
```

Der Jetson-Host soll möglichst unverändert bleiben.

---

# Projektstruktur

Lege im aktuellen VS-Code-Workspace ein nachvollziehbares Projekt an.

Bevorzugte Struktur ungefähr:

```text
.
├── README.md
├── docs/
│   ├── ARCHITECTURE.md
│   ├── SETUP_REPORT.md
│   └── TROUBLESHOOTING.md
│
├── infra/
│   ├── jetson/
│   │   ├── Dockerfile
│   │   ├── build.sh
│   │   ├── run.sh
│   │   └── env.sh
│   └── thinkpad/
│       ├── setup.sh
│       └── env.sh
│
├── scripts/
│   ├── sync_to_jetson.sh
│   ├── test_dds.sh
│   ├── launch_simulation.sh
│   ├── launch_planner.sh
│   └── run_pick_place.sh
│
└── ros2_ws/
    └── src/
        ├── panda_demo_description/
        ├── panda_demo_gazebo/
        ├── panda_demo_moveit_config/
        ├── panda_pick_place/
        └── panda_grasp_adapter/
```

Passe die Struktur an, falls ROS-Konventionen eine bessere Aufteilung nahelegen.

Das Repository soll die Source-of-Truth sein.

Wenn Sourcecode auf dem Jetson benötigt wird, synchronisiere ihn beispielsweise per `rsync` über SSH und mounte ihn anschließend in den Container oder baue ihn in ein reproduzierbares Image.

Keine manuellen, nicht dokumentierten Kopien.

---

# Installation ThinkPad

Installiere bzw. konfiguriere auf Ubuntu 24.04:

```text
ROS 2 Jazzy
ROS development tools
Gazebo Harmonic
ros_gz
gz_ros2_control
ros2_control
ros2_controllers
Cyclone DDS RMW
colcon
rosdep
xacro
RViz, sofern nicht bereits durch ros-jazzy-desktop vorhanden
```

Bevorzuge offizielle Ubuntu-/ROS-Pakete.

Für Gazebo + Jazzy bevorzuge insbesondere den offiziellen ROS-Weg über:

```text
ros-jazzy-ros-gz
ros-jazzy-gz-ros2-control
```

Prüfe Paketnamen mit `apt-cache` bevor du Annahmen triffst.

Keine fremden PPAs hinzufügen, wenn die Pakete bereits über die offiziellen ROS-Repositories verfügbar sind.

---

# Jetson-Docker-Image

Erzeuge einen reproduzierbaren Dockerfile.

Basis bevorzugt:

```dockerfile
FROM ros:jazzy-ros-base-noble
```

Installiere darin nur das, was für Planning benötigt wird, insbesondere:

```text
MoveIt 2
MoveIt Panda Resources / Panda MoveIt Config
OMPL bzw. MoveIt-Abhängigkeiten
Cyclone DDS RMW
colcon
rosdep
development tools für unseren eigenen ROS-Code
```

Bevorzuge apt Binary Packages.

Vor einem möglichen Source-Build:

```bash
apt-cache search ...
apt-cache policy ...
```

ausführen und dokumentieren, warum Source tatsächlich notwendig wäre.

Der Container muss auf `aarch64` laufen.

Teste zunächst mit etwas Einfachem wie:

```bash
ssh jetson 'docker run --rm ros:jazzy-ros-base-noble uname -m'
```

Erwartetes Ergebnis:

```text
aarch64
```

Danach:

```text
ros2 --help
```

im Container testen.

---

# ROS-2-DDS-Integrationstest

Bevor Gazebo oder MoveIt eingerichtet werden, MUSS ROS-Kommunikation über beide Rechner funktionieren.

Test 1:

ThinkPad:

```text
demo_nodes_cpp talker
```

Jetson-Container:

```text
demo_nodes_cpp listener
```

Test 2 genau umgekehrt.

Zusätzlich prüfen:

```text
ros2 node list
ros2 topic list
ros2 topic echo
```

Die Tests müssen über das physische LAN funktionieren.

Beide Seiten müssen:

```text
ROS_DOMAIN_ID=42
RMW_IMPLEMENTATION=rmw_cyclonedds_cpp
```

verwenden.

Erst wenn dieser Test erfolgreich ist, mit Gazebo fortfahren.

---

# Simulationsmodell

Verwende als Robotermodell einen:

```text
Franka Emika Panda
```

Bevorzugt die bestehenden offiziellen MoveIt-Ressourcen:

```text
moveit_resources_panda_description
moveit_resources_panda_moveit_config
```

anstatt unnötig einen eigenen Panda von Grund auf zu modellieren.

Das Robotermodell soll allerdings so integriert werden, dass Gazebo Harmonic und `gz_ros2_control` funktionieren.

Der Roboter soll mindestens besitzen:

```text
7 Arm Joints
Panda Hand
2 Finger Joints
joint_state_broadcaster
joint trajectory controller für den Arm
geeigneten Controller für den Gripper
```

Controller müssen über ROS 2 erreichbar sein.

Die MoveIt-Seite auf dem Jetson muss über das LAN dieselben Controller ansprechen können.

---

# Simulationswelt

Baue eine sehr einfache Welt:

```text
                  PLACE
                    □

        ┌───────────────────────┐
        │         table         │
        │   ■ PICK              │
        └───────────────────────┘

             Panda Robot
```

Mindestens:

```text
Boden
Tisch
Panda
ein würfelförmiges Objekt
```

Beispielsweise:

```text
cube size: ungefähr 4–5 cm
```

Wähle gut erreichbare Positionen.

Keine komplizierte Szene.

---

# Tatsächliches Pick-and-Place

Ich möchte keine reine vorprogrammierte Gelenkanimation.

Der Jetson soll MoveIt verwenden.

Ablauf:

```text
HOME
 ↓
PRE_GRASP
 ↓
GRASP
 ↓
CLOSE_GRIPPER
 ↓
ATTACH OBJECT
 ↓
LIFT
 ↓
PRE_PLACE
 ↓
PLACE
 ↓
DETACH OBJECT
 ↓
OPEN_GRIPPER
 ↓
RETREAT
 ↓
HOME
```

Die Armbewegungen müssen von MoveIt geplant werden.

Der Würfel muss in Gazebo sichtbar von der Pick- zur Place-Position bewegt werden.

---

# Greifen des Objekts

Für dieses Minimalbeispiel ist eine robuste Demonstration wichtiger als perfekte Kontaktphysik.

Bevorzugte Lösung:

Nutze, sofern mit Gazebo Harmonic praktikabel, das eingebaute Gazebo-System:

```text
DetachableJoint
```

um den Würfel beim Greifen am Endeffektor zu befestigen und beim Ablegen wieder zu lösen.

Das soll NICHT bedeuten, dass die Armtrajektorie gefälscht wird.

Nur das Greifen des Würfels darf durch Attach/Detach robust abstrahiert werden.

Bevorzugte Architektur:

```text
Jetson pick_place node
       │
       │ ROS service
       ▼
ThinkPad panda_grasp_adapter
       │
       │ Gazebo Transport
       ▼
DetachableJoint attach/detach
```

Falls `ros_gz_bridge` die benötigten Nachrichten sauber direkt bridgen kann, darf auch dieser Weg verwendet werden.

Falls nicht, implementiere einen sehr kleinen ThinkPad-seitigen Adapter, der ROS-2-Services anbietet:

```text
/grasp/attach
/grasp/detach
```

und lokal mit Gazebo Transport kommuniziert.

Kein SSH-Aufruf während des eigentlichen Pick-and-Place-Ablaufs.

---

# Planning Scene

MoveIt auf dem Jetson soll mindestens kennen:

```text
Tisch
zu greifendes Objekt
```

Füge den Tisch als Collision Object in die Planning Scene ein.

Beim Greifen:

```text
Object → attached collision object
```

Beim Ablegen:

```text
attached object entfernen
Object wieder als World Collision Object einfügen
```

Die MoveIt-Planning-Scene und Gazebo-Szene sollen semantisch konsistent bleiben.

---

# Pick-and-Place Node

Implementiere den Ablauf bevorzugt in C++, sofern dies für MoveIt auf Jazzy der stabilste und idiomatischste Weg ist.

Python ist okay, falls die verwendeten MoveIt-Jazzy-APIs damit sauber unterstützt werden.

Der Node soll nicht nur eine lange Funktion sein.

Verwende eine übersichtliche State Machine oder klar getrennte Schritte:

```text
INITIALIZE
HOME
PRE_GRASP
GRASP
CLOSE
ATTACH
LIFT
PRE_PLACE
PLACE
DETACH
OPEN
RETREAT
HOME
DONE
ERROR
```

Jeder Schritt soll:

```text
klar geloggt werden
Fehler erkennen
nicht einfach blind weitermachen
```

Bei fehlgeschlagener Planung oder Ausführung:

```text
Abbruch mit verständlicher Fehlermeldung
```

statt Folgefehler zu erzeugen.

---

# Nachweis, dass auf dem Jetson geplant wird

Beim Start des Pick-and-Place-Nodes:

```text
Hostname
CPU architecture
ROS distro
ROS_DOMAIN_ID
RMW implementation
```

loggen.

Erwartet beispielsweise:

```text
hostname = user-desktop oder Container/Jetson
architecture = aarch64
ROS_DISTRO = jazzy
```

Außerdem dokumentieren, welche Prozesse auf welchem Host laufen.

Am Ende soll eindeutig nachweisbar sein:

```text
move_group → Jetson
pick_place node → Jetson

Gazebo server → ThinkPad
Gazebo GUI → ThinkPad
ros2_control → ThinkPad
```

---

# Launch-Konzept

Ich möchte am Ende möglichst wenige Befehle benötigen.

Beispielsweise:

Terminal ThinkPad:

```bash
./scripts/launch_simulation.sh
```

Terminal ThinkPad oder automatisiert über SSH:

```bash
./scripts/launch_planner.sh
```

und anschließend:

```bash
./scripts/run_pick_place.sh
```

`launch_planner.sh` darf intern `ssh jetson` verwenden, um den Container zu starten.

Der eigentliche ROS-Ablauf danach soll aber über DDS stattfinden und nicht über SSH simuliert werden.

Wenn sinnvoll, darf der Planner-Container persistent laufen.

---

# Startreihenfolge berücksichtigen

Die Skripte dürfen nicht einfach mit festen `sleep 10` arbeiten und hoffen, dass alles bereit ist.

Bevorzuge Ready-Checks:

```text
Gazebo läuft
joint_state_broadcaster aktiv
arm controller aktiv
/joint_states vorhanden
FollowJointTrajectory Action vorhanden
move_group vorhanden
MoveIt Action Server vorhanden
grasp service vorhanden
```

Erst danach nächsten Schritt starten.

Kurze Timeouts sind okay, aber mit nachvollziehbarer Fehlermeldung.

---

# Tests nach jeder Schicht

Arbeite inkrementell.

## Stage A – Host und Docker

Beweise:

```text
ThinkPad = Ubuntu 24.04 x86_64
Jetson = Ubuntu 18.04 aarch64
ROS Jazzy ARM64 Container startet
```

## Stage B – ROS lokal

Beweise ROS Jazzy auf beiden Seiten.

## Stage C – DDS

Talker/Listener bidirektional zwischen ThinkPad und Jetson.

NICHT weitergehen, wenn das nicht funktioniert.

## Stage D – Gazebo

Gazebo Harmonic startet auf dem ThinkPad.

## Stage E – Panda

Panda erscheint korrekt in Gazebo.

## Stage F – ros2_control

Prüfe beispielsweise:

```bash
ros2 control list_controllers
```

Arm- und Joint-State-Controller müssen aktiv sein.

## Stage G – verteilte Joint States

Im Jetson-Container:

```bash
ros2 topic echo /joint_states
```

muss Daten des simulierten Panda auf dem ThinkPad zeigen.

## Stage H – MoveIt

`move_group` auf dem Jetson muss korrekt starten.

Planning Scene muss geladen werden.

## Stage I – einzelne geplante Bewegung

Noch kein Pick-and-Place.

Plane auf dem Jetson eine einfache Bewegung von HOME zu einer anderen ungefährlichen Pose und sende sie an Gazebo.

Erst wenn das funktioniert, Pick-and-Place implementieren.

## Stage J – Pick and Place

Komplette Sequenz ausführen.

Der Würfel muss sichtbar seinen Ort wechseln.

---

# Akzeptanzkriterien

Das Projekt ist erst erfolgreich, wenn ALLE folgenden Punkte erfüllt sind:

```text
[ ] ThinkPad läuft mit ROS 2 Jazzy
[ ] ThinkPad läuft mit Gazebo Harmonic
[ ] Jetson Host bleibt Ubuntu 18.04 / L4T R32.7.6
[ ] Jetson führt ROS 2 Jazzy in ARM64 Docker aus
[ ] ROS 2 Kommunikation funktioniert bidirektional über LAN
[ ] Panda wird in Gazebo auf dem ThinkPad simuliert
[ ] /joint_states erreichen den Jetson
[ ] move_group läuft tatsächlich auf dem Jetson
[ ] Motion Planning findet tatsächlich auf dem Jetson statt
[ ] geplante Trajektorie erreicht ros2_control auf dem ThinkPad
[ ] Panda bewegt sich entsprechend in Gazebo
[ ] Pick-and-Place wird über MoveIt geplant
[ ] Würfel wird sichtbar aufgenommen
[ ] Würfel wird sichtbar an anderer Stelle abgelegt
[ ] keine Gazebo-GUI auf dem Jetson
[ ] kein ROS direkt auf Ubuntu 18.04 des Jetson installiert
[ ] Setup ist reproduzierbar dokumentiert
```

---

# Qualität

Bitte keine Wegwerf-Lösung bauen.

Gleichzeitig keine unnötige Enterprise-Architektur.

Prioritäten:

```text
1. Funktioniert reproduzierbar
2. Architektur ist nachvollziehbar
3. Planner und Simulation sind tatsächlich getrennt
4. ROS-2-Mechanismen werden idiomatisch verwendet
5. Setup bleibt einfach
6. Gute Dokumentation
```

Shell-Skripte sollen:

```text
set -euo pipefail
```

verwenden, sofern sinnvoll.

Alle Skripte idempotent gestalten.

Keine Zugangsdaten oder Passwörter speichern.

Keine `sudo`-Passwörter automatisieren.

Keine unnötigen globalen Änderungen an `.bashrc`.

Stattdessen lieber projektspezifische:

```text
env.sh
```

Dateien verwenden.

---

# Dokumentation

Erzeuge ein gutes `README.md`.

Es soll mindestens erklären:

```text
Architektur
Voraussetzungen
Setup
Build
Start
Pick-and-Place ausführen
Stoppen
Troubleshooting
```

Erzeuge außerdem:

```text
docs/ARCHITECTURE.md
```

mit einem Mermaid-Diagramm ungefähr:

```text
ThinkPad                             Jetson Nano
─────────────────                    ───────────────────
Gazebo Harmonic                      Docker
      │                                 │
gz_ros2_control                         ROS 2 Jazzy
      │                                 │
ros2_control ◄────────────────────── MoveIt 2
      │          trajectory             │
      │                                 │
joint_states ───────────────────────► move_group
      │
Gazebo GUI
```

und:

```text
docs/SETUP_REPORT.md
```

mit:

```text
festgestellten Versionen
IP-/Interface-Situation
installierten Paketen
Docker Image
Testergebnissen
eventuellen Abweichungen
```

---

# Vorgehensweise für dich als Coding Agent

Du hast Zugriff auf die Shell des ThinkPads und über:

```bash
ssh jetson
```

auf den Jetson.

Nutze diesen Zugriff aktiv.

Rate nicht, wenn du etwas direkt prüfen kannst.

Wenn ein Befehl fehlschlägt:

1. Fehlerausgabe lesen.
2. Ursache bestimmen.
3. Minimalen Fix durchführen.
4. Test wiederholen.
5. Änderung dokumentieren.

Nicht einfach alternative Pakete installieren, bis zufällig etwas funktioniert.

Wenn du bei einer technischen Entscheidung zwischen mehreren Varianten wählen musst, bevorzuge:

```text
offizielle ROS-2-Jazzy-Pakete
offizielle Gazebo-Harmonic-Pakete
offizielle MoveIt-Ressourcen
standardisierte ROS-2-Interfaces
kleine nachvollziehbare Eigenimplementierungen
```

gegenüber:

```text
alten Tutorials
ROS 1
Gazebo Classic
inoffiziellen PPAs
großen Source-Builds
Workarounds über SSH
hart codierten IP-Adressen
```

---

# Wichtige Versionsregel

Wir verwenden:

```text
ROS 2 Jazzy
Gazebo Harmonic
Ubuntu 24.04 im Jetson-Container
Ubuntu 24.04 nativ auf dem ThinkPad
```

Nicht versehentlich Tutorials für:

```text
ROS Humble
ROS Foxy
Gazebo Classic
Ignition Gazebo alte Versionen
```

übernehmen.

Wenn Online-Dokumentation oder Beispiele widersprüchlich sind, prüfe ausdrücklich, ob sie für **ROS 2 Jazzy + Gazebo Harmonic** gelten.

---

# Beginne jetzt

Starte jetzt mit der Bestandsaufnahme auf beiden Hosts.

Berichte kurz den erkannten Ist-Zustand.

Führe danach das Setup schrittweise selbstständig durch.

Frage mich nur dann nach Eingriffen, wenn etwas wirklich nicht automatisierbar ist, beispielsweise ein interaktives sudo-Passwort.

Ansonsten arbeite die Stages A–J selbstständig nacheinander ab und teste jede Stage, bevor du fortfährst.

Am Ende möchte ich:

```text
1. ein funktionierendes System
2. den vollständigen Sourcecode
3. reproduzierbare Setup-/Launch-Skripte
4. README + Architektur-Dokumentation
5. einen Bericht über alle erfolgreich durchgeführten Tests
```