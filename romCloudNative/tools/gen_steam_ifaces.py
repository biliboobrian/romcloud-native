#!/usr/bin/env python3
"""Tables virtuelles des interfaces de l'ancienne API Steam (rcn_steam_legacy_tables.h).

Les jeux qui prennent « SteamApps() », « SteamUser() »… au DLL appellent ensuite les méthodes par la
table virtuelle de l'interface. Cette table est celle du SDK Steamworks avec lequel le jeu a été
compilé : les versions suivantes insèrent parfois des méthodes au milieu, donc une table d'un SDK plus
récent décalerait les appels et la pile de l'appelant. Deux exemples :

  - ISteamRemoteStorage v007 → v008 insère les FileWriteStream* avant FileExists (tout décale) ;
  - ISteamUserStats v010 → v011 insère GetNumAchievements et GetAchievementName après
    IndicateAchievementProgress : RequestUserStats passe de l'indice 14 à 16, GetUserStat de 15/16 à
    17/18, etc.

Dishonored (août 2013) est bâti sur le SDK 1.18/1.19 : son steam_api.ini de secours (émulateur) donne
même la liste exacte des versions — SteamApps 005, SteamFriends 011, SteamUser 016, SteamUserStats 010,
SteamUtils 005, SteamClient 012, SteamRemoteStorage 006, SteamNetworking 005. Ce générateur lit donc
les en-têtes du SDK 1.18 et écrit les tables dans l'ordre des déclarations des classes :

  python tools/gen_steam_ifaces.py <dossier>/steamworks_sdk_118 runtime/rcn_steam_legacy_tables.h

Les en-têtes se trouvent dans lsteamclient/steamworks_sdk_118 du dépôt Proton
(https://github.com/ValveSoftware/Proton) ; ils ne sont pas recopiés ici.
"""
import os
import re
import sys

# Interface → nom de l'accesseur global exporté par steam_api.dll, et identifiant court.
INTERFACES = [
    ("ISteamApps", "SteamApps", "apps"),
    ("ISteamFriends", "SteamFriends", "friends"),
    ("ISteamGameServer", "SteamGameServer", "gameserver"),
    ("ISteamMatchmaking", "SteamMatchmaking", "matchmaking"),
    ("ISteamMatchmakingServers", "SteamMatchmakingServers", "matchmakingservers"),
    ("ISteamNetworking", "SteamNetworking", "networking"),
    ("ISteamRemoteStorage", "SteamRemoteStorage", "remotestorage"),
    ("ISteamUser", "SteamUser", "user"),
    ("ISteamUserStats", "SteamUserStats", "userstats"),
    ("ISteamUtils", "SteamUtils", "utils"),
]

# Taille d'un argument en 32 bits : les types plus petits qu'un mot sont poussés sur un mot, les
# entiers de 64 bits et les doubles sur deux (la méthode les retire, __thiscall).
SIZES = {
    "bool": 4, "char": 4, "int8": 4, "uint8": 4, "int16": 4, "uint16": 4,
    "short": 4, "unsigned short": 4, "int": 4, "unsigned int": 4, "long": 4,
    "unsigned long": 4, "int32": 4, "uint32": 4, "float": 4, "HSteamPipe": 4, "HSteamUser": 4,
    "HSteamListenSocket": 4, "HSteamNetConnection": 4,
    "int64": 8, "uint64": 8, "double": 8,
    # Identifiants et poignées de 64 bits, passés par valeur.
    "CGameID": 8, "CSteamID": 8, "SteamAPICall_t": 8, "UGCHandle_t": 8, "PublishedFileId_t": 8,
    "PublishedFileUpdateHandle_t": 8, "UGCFileWriteStreamHandle_t": 8, "SteamLeaderboard_t": 8,
    "SteamLeaderboardEntries_t": 8, "SteamItemInstanceID_t": 8, "GID_t": 8,
    # Structure passée par valeur (16 octets d'adresse + type), alignée sur 4.
    "SteamIPAddress_t": 20,
}


def value_size(param_type):
    if "*" in param_type:
        return 4
    param_type = param_type.strip()
    if param_type in SIZES:
        return SIZES[param_type]
    # Énumérations (ELeaderboardSortMethod…), poignées (H…, …_t) : un mot.
    if param_type.startswith("E") or param_type.startswith("H") or param_type.endswith("_t"):
        return 4
    raise SystemExit(f"type d'argument inconnu : {param_type!r}")


def class_body(text, name):
    # Un en-tête déclare aussi les interfaces qu'il rend (« class ISteamFriends; ») : ces déclarations
    # anticipées n'ont pas de corps, et prendre le « }; » suivant donnerait la classe voisine.
    for found in re.finditer(r"class\s+" + name + r"\b", text):
        if re.match(r"\s*;", text[found.end():found.end() + 8]):
            continue
        end = text.find("};", found.start())
        if end < 0:
            continue
        return text[found.start():end]
    return None


def methods(body):
    # Commentaires et retours à la ligne retirés : une déclaration tient alors sur une ligne.
    body = re.sub(r"//[^\n]*", "", body).replace("\r", " ")
    body = re.sub(r"\s+", " ", body)
    found = []
    for declaration in re.finditer(r"virtual\s+([^;{}]+?)\b(\w+)\s*\(([^)]*)\)\s*=\s*0\s*;", body):
        returned = declaration.group(1).strip()
        name, params = declaration.group(2), declaration.group(3).strip()
        words = 0
        # Un CSteamID (classe à constructeur de copie) rendu par valeur passe par un pointeur caché :
        # Visual C++ l'ajoute en premier argument sur la pile, et la méthode le retire comme les autres.
        if returned in ("CSteamID", "CGameID"):
            words += 1
        if params and params != "void":
            for param in params.split(","):
                param = param.split("=")[0].strip()
                if "*" in param:
                    kind = param[:param.rfind("*") + 1]
                else:
                    kind = param.rsplit(" ", 1)[0]
                kind = re.sub(r"\s*\*\s*", "*", kind.replace("const ", "")
                              .replace("struct ", "").replace("class ", "").strip())
                words += value_size(kind) // 4
        found.append((name, words))
    if not found:
        raise SystemExit("aucune méthode trouvée")
    return found


def main():
    if len(sys.argv) != 3:
        raise SystemExit(__doc__)
    root = sys.argv[1]
    out_path = sys.argv[2]
    headers = {}
    for name in sorted(os.listdir(root)):
        if name.endswith(".h"):
            with open(os.path.join(root, name), encoding="utf-8", errors="replace") as f:
                headers[name] = f.read()

    tables = []
    for classname, accessor, short in INTERFACES:
        body = None
        for text in headers.values():
            body = class_body(text, classname)
            if body:
                break
        if not body:
            raise SystemExit(f"interface absente des en-têtes : {classname}")
        tables.append((classname, accessor, short, methods(body)))

    out = [
        "// RomCloud Native : tables virtuelles des interfaces de l'ancienne API Steam.",
        "// GÉNÉRÉ par tools/gen_steam_ifaces.py depuis les en-têtes du SDK Steamworks 1.18",
        "// (lsteamclient/steamworks_sdk_118 de Proton) : c'est la table des jeux de l'époque",
        "// (Dishonored, 32 bits). Ne pas modifier à la main.",
        "",
    ]
    for classname, accessor, short, methods_ in tables:
        out.append(f"// {classname} : {len(methods_)} méthodes (accesseur « {accessor} »).")
        out.append(f"static const RcnMethod steam_{short}_methods[] = {{")
        for name, words in methods_:
            out.append(f"    {{\"{name}\", steam_legacy_method, {words}}},")
        out.append("};")
        out.append("")
    out.append("// Une fonction d'accès par interface : elle rend l'objet dont la table virtuelle est ci-dessus.")
    for classname, accessor, short, _ in tables:
        out.append(f"static void steam_iface_{short}(RcnCtx *ctx) {{")
        out.append(f"    RET(steam_object(ctx, \"{classname}\", steam_{short}_methods,")
        out.append(f"                                sizeof steam_{short}_methods / sizeof steam_{short}_methods[0]));")
        out.append("}")
    out.append("")
    out.append("// Accesseurs de l'ancienne API, importés directement par les jeux (« SteamApps », « SteamUser »…).")
    out.append("static const struct {")
    out.append("    const char *accessor;")
    out.append("    RcnFn fn;")
    out.append("} steam_legacy_accessors[] = {")
    for classname, accessor, short, _ in tables:
        out.append(f"    {{\"{accessor}\", steam_iface_{short}}},")
    out.append("};")
    out.append("")
    with open(out_path, "w", encoding="utf-8", newline="\n") as f:
        f.write("\n".join(out))


if __name__ == "__main__":
    main()
