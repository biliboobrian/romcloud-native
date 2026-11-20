#!/usr/bin/env python3
"""Tables virtuelles des interfaces de l'ancienne API Steam (rcn_steam_legacy_tables.h).

Lit public/steam/steam_api.json du SDK Steamworks et écrit, pour chaque interface demandée par les
jeux de l'époque (SteamApps(), SteamUser()…), la liste ordonnée de ses méthodes : c'est l'ordre de la
table virtuelle, donc celui dans lequel le jeu appelle les méthodes. Les versions d'interface
n'ajoutent que des méthodes à la fin : la table du SDK récent sert aussi les jeux plus anciens.

  python tools/gen_steam_ifaces.py <chemin>/steam_api.json runtime/rcn_steam_legacy_tables.h
"""
import json
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


def main():
    if len(sys.argv) != 3:
        raise SystemExit(__doc__)
    data = json.load(open(sys.argv[1], encoding="utf-8"))
    out_path = sys.argv[2]
    by_name = {i["classname"]: i for i in data["interfaces"]}
    out = [
        "// RomCloud Native : tables virtuelles des interfaces de l'ancienne API Steam.",
        "// GÉNÉRÉ par tools/gen_steam_ifaces.py depuis public/steam/steam_api.json du SDK Steamworks :",
        "// ne pas modifier à la main. L'ordre est celui des méthodes dans l'interface (table virtuelle).",
        "",
    ]
    for classname, accessor, short in INTERFACES:
        methods = by_name[classname]["methods"]
        out.append(f"// {classname} : {len(methods)} méthodes (accesseur « {accessor} »).")
        out.append(f"static const RcnMethod steam_{short}_methods[] = {{")
        for method in methods:
            words = sum(value_size(p["paramtype"]) for p in method["params"]) // 4
            out.append(f"    {{\"{method['methodname']}\", steam_legacy_method, {words}}},")
        out.append("};")
        out.append("")
    out.append("// Une fonction d'accès par interface : elle rend l'objet dont la table virtuelle est ci-dessus.")
    for classname, accessor, short in INTERFACES:
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
    for classname, accessor, short in INTERFACES:
        out.append(f"    {{\"{accessor}\", steam_iface_{short}}},")
    out.append("};")
    out.append("")
    with open(out_path, "w", encoding="utf-8", newline="\n") as f:
        f.write("\n".join(out))


if __name__ == "__main__":
    main()
