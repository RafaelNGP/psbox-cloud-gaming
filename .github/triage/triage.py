#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
# Copyright (C) 2026 RafaelNGP
"""Triage of a new or edited issue, by rules (no AI, nothing to pay).

Reads the bug form's fields (What happened?, App release, PlayStation Firmware,
xcloud.log), matches rules.json's known problems, and then on the issue:
adds labels (never removes one), and writes or updates ONE comment (marked
with MARKER) in the reporter's language (English, Portuguese, or Spanish)
saying what is known, asking to update when the release is older than the
fix or the latest, and asking for what is missing.

A new issue that repeats one the same person opened minutes before (the same
text, sent again) gets one line pointing at the first and is closed as a
duplicate; the first one stays and is triaged as usual.

The issue's text is someone else's: it is only matched with regular
expressions here, never run or put in a command.

  GitHub Actions: triage.py            (GITHUB_EVENT_PATH, GITHUB_TOKEN, GITHUB_REPOSITORY;
                                        TRIAGE_ISSUE=<number> for one already there)
  Locally:        triage.py --dry-run issue.json --latest 1.3.2
                  (issue.json: {"title": ..., "body": ...}, e.g. from `gh issue view N --json title,body`)
"""
import datetime
import json
import os
import re
import sys
import urllib.error
import urllib.parse
import urllib.request

MARKER = "<!-- psbox-triage -->"
HERE = os.path.dirname(os.path.abspath(__file__))
MAX_TEXT = 200_000  # characters of an issue looked at
DUPLICATE_WINDOW_MIN = 60  # the same person, the same text, this long after the first: a repeat
# What the app writes by itself when the report has no text of the person's: it says nothing about the problem.
PLACEHOLDER_WHAT = re.compile(r"^(reported from settings|problem report)?$", re.I)
PLACEHOLDER_TITLE = re.compile(r"^(problem report|bug|\[bug\]:?|ps[45])?$", re.I)
LATEST_RELEASE_URL = "https://github.com/RafaelNGP/psbox-cloud-gaming/releases/latest"

LABEL_COLOURS = {
    "needs-info": "fbca04",
    "outdated-version": "c5def5",
    "crash": "b60205",
    "unofficial-port": "d93f0b",
    "unsupported-platform": "d93f0b",
    "duplicate": "cfd3d7",
}
AREA_COLOUR = "1d76db"
FIRMWARE_COLOUR = "ededed"

STRINGS = {
    "en": {
        "header": "Automatic triage (rules, not a person; the maintainer will read this too):",
        "update_rule": " You're on v{current}: please update to v{fixed} or newer.",
        "outdated_latest": (
            "- This was reported on v{current}; the latest is **v{latest}** "
            "(the app offers it after sign-in, or Settings > Updates). Does it still happen there?"
        ),
        "unsupported_147": (
            "- **Unofficial port (v1.4.7)**: Version 1.4.7 is an unofficial port for PS4 created by a third party. "
            "It was not created or maintained by the author of this repository, so official support cannot be provided for that build.\n\n"
            "This repository develops official native versions for **PlayStation 5** (PPSA99810), **PlayStation 4** (PKG), and **Linux** (AppImage). "
            "Please download the correct official version from our latest release:\n"
            f"👉 {LATEST_RELEASE_URL}\n\n"
            "This issue is being closed as an unofficial port.\n\n"
            "*(Tip: If you are having issues claiming games or managing your account, visit https://www.xbox.com/play "
            "in a PC or mobile browser to manage your account and claim titles directly).*"
        ),
        "empty_title_known": (
            "\n*(This issue was submitted with only a title. As it matches a known problem, "
            "the solution above has been provided automatically and this issue is now closed.)*"
        ),
        "duplicate": (
            "Automatic triage: this is the same report as #{first}, sent a few minutes before by you. One is enough: this one is closed, and #{first} is where it is followed (add anything new there)."
        ),
        "ask_release": "the **app release** (Settings > Updates shows it, e.g. v1.3.2)",
        "ask_log": (
            "the **xcloud.log** (`/data/psbox/xcloud.log` over FTP; before v0.9.7 in `/data/homebrew/PPSA99810/`, "
            "or use *Report a problem* (△ in Settings or on the error) to fill this form from the app)"
        ),
        "ask_join": " and ",
        "ask_line": "- To look into it, please add {asks}.",
        "reddit_invite": (
            "💬 **Join our community on Reddit**: Visit [r/PSBoxCloudGaming](https://www.reddit.com/r/PSBoxCloudGaming/) "
            "for discussions, updates, and community support!"
        ),
    },
    "pt": {
        "header": "Triagem automática (regras automáticas; o mantenedor também lerá esta issue):",
        "update_rule": " Você está na v{current}: por favor, atualize para a v{fixed} ou mais recente.",
        "outdated_latest": (
            "- Este relato foi enviado na v{current}; a versão mais recente é a **v{latest}** "
            "(o app oferece a atualização após o login ou em Configurações > Atualizações). O problema ainda ocorre nela?"
        ),
        "unsupported_147": (
            "- **Port não-oficial (v1.4.7)**: A versão 1.4.7 é um port/modificação não-oficial para PS4 desenvolvida por terceiros. "
            "Não é possível oferecer suporte técnico, investigar problemas ou realizar correções para essa versão, pois ela não foi feita pelo mantenedor deste projeto.\n\n"
            "Este repositório desenvolve versões oficiais nativas para **PlayStation 5** (PPSA99810), **PlayStation 4** (PKG) e **Linux** (AppImage). "
            "Por favor, baixe a versão oficial correta na nossa release mais recente:\n"
            f"👉 {LATEST_RELEASE_URL}\n\n"
            "Esta issue está sendo encerrada por se tratar de um port não-oficial.\n\n"
            "*(Dica: Se estiver com problemas para resgatar jogos gratuitos ou gerenciar sua conta, acesse https://www.xbox.com/play "
            "pelo navegador de um computador ou celular para resgatar os títulos diretamente na sua conta Xbox).*"
        ),
        "empty_title_known": (
            "\n*(Esta issue foi enviada apenas com o título. Como corresponde a um problema já conhecido, "
            "a orientação acima foi fornecida automaticamente e a issue está sendo encerrada.)*"
        ),
        "duplicate": (
            "Triagem automática: este é o mesmo relato da #{first}, enviado por você poucos minutos antes. Um só basta: esta issue está sendo fechada e o acompanhamento segue na #{first} (acrescente lá qualquer informação nova)."
        ),
        "ask_release": "a **versão do app** (mostrada em Configurações > Atualizações, ex.: v1.3.2)",
        "ask_log": (
            "o **xcloud.log** (`/data/psbox/xcloud.log` via FTP, ou use *Relatar um problema* "
            "(△ nas Configurações ou na tela de erro) para preencher este formulário pelo app)"
        ),
        "ask_join": " e ",
        "ask_line": "- Para investigarmos melhor, por favor adicione {asks}.",
        "reddit_invite": (
            "💬 **Participe da nossa comunidade no Reddit**: Acesse [r/PSBoxCloudGaming](https://www.reddit.com/r/PSBoxCloudGaming/) "
            "para novidades, dicas e suporte da comunidade!"
        ),
    },
    "es": {
        "header": "Triaje automático (reglas automáticas; el mantenedor también leerá este reporte):",
        "update_rule": " Estás en la v{current}: por favor, actualiza a la v{fixed} o superior.",
        "outdated_latest": (
            "- Este reporte fue enviado en la v{current}; la versión más reciente es la **v{latest}** "
            "(la app ofrece actualizar tras iniciar sesión o en Configuración > Actualizaciones). ¿Sigue ocurriendo ahí?"
        ),
        "unsupported_147": (
            "- **Port no oficial (v1.4.7)**: La versión 1.4.7 es una modificación/port no oficial para PS4 desarrollado por terceros. "
            "No es posible ofrecer asistencia técnica, investigar problemas ni publicar correcciones para esa versión, ya que no fue creada por el mantenedor de este proyecto.\n\n"
            "Este repositorio desarrolla versiones oficiales nativas para **PlayStation 5** (PPSA99810), **PlayStation 4** (PKG) y **Linux** (AppImage). "
            "Por favor, descarga la versión oficial correcta desde nuestra release más reciente:\n"
            f"👉 {LATEST_RELEASE_URL}\n\n"
            "Este reporte se cierra por tratarse de un port no oficial.\n\n"
            "*(Consejo: Si tienes dificultades para reclamar juegos gratuitos o gestionar tu cuenta, accede a https://www.xbox.com/play "
            "desde el navegador de un ordenador o móvil para reclamar los títulos directamente en tu cuenta de Xbox).*"
        ),
        "empty_title_known": (
            "\n*(Esta incidencia se envió únicamente con el título. Dado que coincide con un problema conocido, "
            "la solución anterior se ha proporcionado automáticamente y se procede a cerrar la incidencia.)*"
        ),
        "duplicate": (
            "Triaje automático: este es el mismo reporte que #{first}, enviado por ti unos minutos antes. Con uno basta: este se cierra y el seguimiento continúa en #{first} (añade allí cualquier información nueva)."
        ),
        "ask_release": "la **versión de la app** (se muestra en Configuración > Actualizaciones, ej.: v1.3.2)",
        "ask_log": (
            "el **xcloud.log** (`/data/psbox/xcloud.log` por FTP, o usa *Informar de un problema* "
            "(△ en Configuración o en la pantalla de error) para rellenar este formulario desde la app)"
        ),
        "ask_join": " y ",
        "ask_line": "- Para revisarlo, por favor añade {asks}.",
        "reddit_invite": (
            "💬 **Únete a nuestra comunidad en Reddit**: ¡Visita [r/PSBoxCloudGaming](https://www.reddit.com/r/PSBoxCloudGaming/) "
            "para novedades, consejos y soporte de la comunidad!"
        ),
    },
}

PT_WORDS = re.compile(
    r"[ãõç]|\b(n[aã]o|consigo|jogo|jogos|resgatar|erro ao|iniciar sess[aã]o|posso|quando|"
    r"aparece|tela|ajuda|ol[aá]|obrigado|bot[aã]o|controle|carrega|logar|minha|meu|fazer)\b",
    re.I,
)
ES_WORDS = re.compile(
    r"[¿¡ñ]|\b(no me|juego|juegos|adquirir|error al|iniciar sesi[oó]n|puedo|consola|cuando|"
    r"sale|pantalla|ayuda|hola|gracias|bot[oó]n|mando|carga|guarda|hacer|est[aá]n)\b",
    re.I,
)
EN_WORDS = re.compile(
    r"\b(the|when|doesn't|does not|can't|cannot|failed|while|after|before|screen|button|"
    r"controller|game|sign-in|login|started| freezes|froze|crashed|issue|without)\b",
    re.I,
)


def detect_lang(title, what, log):
    """Detects 'pt', 'es', or 'en' from the reporter's text or the log's language setting."""
    user_text = f"{title or ''}\n{what or ''}"
    pt_hits = len(PT_WORDS.findall(user_text))
    es_hits = len(ES_WORDS.findall(user_text))
    en_hits = len(EN_WORDS.findall(user_text))
    if pt_hits > es_hits and pt_hits >= en_hits and pt_hits > 0:
        return "pt"
    if es_hits > pt_hits and es_hits >= en_hits and es_hits > 0:
        return "es"
    if en_hits == 0:
        m = re.search(r"settings:\s+language\s+(pt|es|en)\b", log or "", re.I)
        if m:
            return m.group(1).lower()
    return "en"


def parse_form(body):
    """The issue form's sections: '### Heading' -> text (without _No response_ or code fences)."""
    fields = {}
    current = None
    lines = []
    for line in (body or "")[:MAX_TEXT].splitlines():
        m = re.match(r"^###\s+(.+?)\s*$", line)
        if m:
            if current is not None:
                fields[current] = "\n".join(lines).strip()
            current, lines = m.group(1).lower(), []
        elif current is not None:
            lines.append(line)
    if current is not None:
        fields[current] = "\n".join(lines).strip()
    for key, value in fields.items():
        value = re.sub(r"^```[a-z]*\s*$", "", value, flags=re.M).strip()
        fields[key] = "" if value == "_No response_" else value
    return fields


def field(fields, *names):
    for key, value in fields.items():
        if any(key.startswith(n) for n in names):
            return value
    return ""


def version_tuple(text):
    m = re.search(r"(\d+)\.(\d+)\.(\d+)", text or "")
    return tuple(int(x) for x in m.groups()) if m else None


def firmware_of(text):
    """'13.60', '5.50' ... or None (a version of the app typed there isn't a firmware)."""
    m = re.search(r"\b(\d{1,2})\.(\d{2})\b", text or "")
    if not m or version_tuple(text):
        return None
    major = int(m.group(1))
    return f"{major}.{m.group(2)}" if 1 <= major <= 20 else None


def looks_like_log(text):
    return bool(re.search(r"\d\d:\d\d:\d\d \[[IWED]\]", text or ""))


def is_empty_issue(body, fields):
    """Returns True if the issue has no description/content beyond its title."""
    if not (body or "").strip():
        return True
    cleaned = (body or "").strip()
    if cleaned in ("_No response_", "No description provided"):
        return True
    if fields:
        return all(not v.strip() for v in fields.values())
    return False


def is_unsupported_147(fields, release, title, body):
    """Detects issues reporting version 1.4.7 (the unofficial third-party PS4 port).

    "PS4" alone in the firmware/platform field is not enough: there is an official
    PS4 build now, and its users must get the normal triage instead of a close."""
    if release == (1, 4, 7):
        return True
    app_rel = field(fields, "app release")
    if re.search(r"\bv?1\.4\.7\b", app_rel, re.I):
        return True
    text = f"{title or ''}\n{body or ''}"
    if re.search(r"\bv?1\.4\.7\b", text, re.I):
        return True
    return False


def report_key(issue):
    """What a report says, to compare two of them: (the person's text, the title), lower
    case, spaces as one; "" for the one that says nothing (the app's own placeholder)."""
    title = (issue.get("title") or "")[:1000]
    body = (issue.get("body") or "")[:MAX_TEXT]
    fields = parse_form(body)
    what = field(fields, "what happened") if fields else body
    norm = lambda t: re.sub(r"\s+", " ", t).strip().lower()
    what, title = norm(what), norm(title)
    return ("" if PLACEHOLDER_WHAT.match(what) else what, "" if PLACEHOLDER_TITLE.match(title) else title)


def same_report(a, b):
    """The same text, or the same title (the app's reports carry the error in both; some come with one empty)."""
    return bool((a[0] and a[0] == b[0]) or (a[1] and a[1] == b[1]))


def created_at(issue):
    try:
        return datetime.datetime.fromisoformat((issue.get("created_at") or "").replace("Z", "+00:00"))
    except ValueError:
        return None


def duplicate_of(issue, earlier, window_min=DUPLICATE_WINDOW_MIN):
    """The number of the first issue this one repeats, or None. `earlier`: the same
    person's other issues. A repeat: opened before this one, at most window_min
    minutes before, saying the same (same_report)."""
    key, when = report_key(issue), created_at(issue)
    if not any(key) or when is None:
        return None
    firsts = []
    for other in earlier:
        other_when = created_at(other)
        if other.get("pull_request") or other_when is None or other.get("number") is None:
            continue
        if other["number"] >= issue.get("number", 0) or other_when > when:
            continue
        if (when - other_when).total_seconds() > window_min * 60:
            continue
        if same_report(report_key(other), key):
            firsts.append(other["number"])
    return min(firsts) if firsts else None


def duplicate_comment(issue, first):
    title = (issue.get("title") or "")[:1000]
    fields = parse_form((issue.get("body") or "")[:MAX_TEXT])
    lang = detect_lang(title, field(fields, "what happened") if fields else issue.get("body") or "", field(fields, "xcloud.log"))
    author = (issue.get("user") or {}).get("login") or issue.get("author")
    return "\n".join([MARKER] + ([f"@{author}"] if author else []) + [STRINGS[lang]["duplicate"].format(first=first)])


def triage(issue, rules, latest):
    """-> (labels to add, comment text or None, should_close bool)."""
    title = (issue.get("title") or "")[:1000]
    body = (issue.get("body") or "")[:MAX_TEXT]
    fields = parse_form(body)
    what = field(fields, "what happened")
    log = field(fields, "xcloud.log")
    parts = {"title": title, "what": what, "log": log}
    if not fields:  # not the form: all of it is "what happened"
        parts["what"] = body
    release = version_tuple(field(fields, "app release"))
    firmware = firmware_of(field(fields, "playstation firmware", "ps5 firmware"))
    latest_t = version_tuple(latest) if latest else None
    lang = detect_lang(title, parts["what"], log)
    tr = STRINGS[lang]
    reply_key = f"reply_{lang}" if lang != "en" else "reply"
    author = (issue.get("user") or {}).get("login") or issue.get("author")
    mention_line = [f"@{author}"] if author else []

    if is_unsupported_147(fields, release, title, body):
        labels = ["unofficial-port"]
        out = [MARKER] + mention_line + [tr["header"], "", tr["unsupported_147"], "", tr["reddit_invite"]]
        return labels, "\n".join(out), "not_planned"

    empty_issue = is_empty_issue(body, fields)

    labels, notes = [], []
    asked_update = False
    for rule in rules:
        where = rule.get("where", ["title", "what", "log"])
        text = "\n".join(parts[w] for w in where)
        if not any(re.search(p, text, re.I) for p in rule["patterns"]):
            continue
        if rule["label"] not in labels:
            labels.append(rule["label"])
        note = rule.get(reply_key) or rule["reply"]
        fixed = version_tuple(rule.get("fixed_in"))
        if fixed and release and release < fixed:
            note += tr["update_rule"].format(current=".".join(map(str, release)), fixed=rule["fixed_in"])
            asked_update = True
        notes.append(note)

    if empty_issue:
        if notes:
            out = [MARKER] + mention_line + [tr["header"], ""]
            for note in notes:
                out.append(f"- {note}")
            out.append(tr["empty_title_known"])
            out.append("")
            out.append(tr["reddit_invite"])
            return labels, "\n".join(out), "completed"
        return labels, None, False

    asks = []
    if not fields:  # not the bug form (a question, an idea): nothing to ask for
        pass
    elif not release:
        asks.append(tr["ask_release"])
    if fields and not looks_like_log(log):
        asks.append(tr["ask_log"])
    if asks:
        labels.append("needs-info")
    outdated = release and latest_t and release < latest_t
    if outdated:
        labels.append("outdated-version")
    if firmware:
        labels.append(f"fw: {firmware}")

    if not notes and not asks and not outdated:
        return labels, None, False
    out = [MARKER] + mention_line + [tr["header"], ""]
    for note in notes:
        out.append(f"- {note}")
    if outdated and not asked_update:
        out.append(tr["outdated_latest"].format(current=".".join(map(str, release)), latest=latest))
    if asks:
        out.append(tr["ask_line"].format(asks=tr["ask_join"].join(asks)))
    out.append("")
    out.append(tr["reddit_invite"])
    return labels, "\n".join(out), False


# --- GitHub ---------------------------------------------------------------------

def api(method, path, token, data=None):
    req = urllib.request.Request(
        "https://api.github.com" + path,
        method=method,
        data=None if data is None else json.dumps(data).encode(),
        headers={"Authorization": f"Bearer {token}", "Accept": "application/vnd.github+json",
                 "X-GitHub-Api-Version": "2022-11-28", "User-Agent": "psbox-triage"})
    with urllib.request.urlopen(req, timeout=30) as r:
        text = r.read().decode()
        return json.loads(text) if text else None


def label_colour(name):
    if name.startswith("area: "):
        return AREA_COLOUR
    if name.startswith("fw: "):
        return FIRMWARE_COLOUR
    return LABEL_COLOURS.get(name, "ededed")


def apply(repo, number, labels, comment, token, close=False):
    for name in labels:  # made the first time they're needed
        try:
            api("POST", f"/repos/{repo}/labels", token, {"name": name, "color": label_colour(name)})
        except urllib.error.HTTPError as e:
            if e.code != 422:  # 422: it exists
                raise
    if labels:
        api("POST", f"/repos/{repo}/issues/{number}/labels", token, {"labels": labels})
    if comment is not None:
        mine = [c for c in api("GET", f"/repos/{repo}/issues/{number}/comments?per_page=100", token)
                if MARKER in (c.get("body") or "") and c.get("user", {}).get("type") == "Bot"]
        if mine:
            try:
                if mine[0]["body"] != comment:
                    api("PATCH", f"/repos/{repo}/issues/comments/{mine[0]['id']}", token, {"body": comment})
            except urllib.error.HTTPError:
                api("POST", f"/repos/{repo}/issues/{number}/comments", token, {"body": comment})
        else:
            api("POST", f"/repos/{repo}/issues/{number}/comments", token, {"body": comment})
    if close:
        try:
            state_reason = close if isinstance(close, str) else "not_planned"
            api("PATCH", f"/repos/{repo}/issues/{number}", token, {"state": "closed", "state_reason": state_reason})
        except urllib.error.HTTPError as e:
            print(f"Warning: could not close issue #{number}: {e}")


def main(argv):
    rules = json.load(open(os.path.join(HERE, "rules.json")))["rules"]
    if len(argv) >= 3 and argv[1] == "--dry-run":
        issue = json.load(open(argv[2]))
        latest = argv[argv.index("--latest") + 1] if "--latest" in argv else None
        labels, comment, should_close = triage(issue, rules, latest)
        print("labels:", labels)
        print(comment or "(no comment)")
        print(f"close: {should_close}")
        return 0
    repo, token = os.environ["GITHUB_REPOSITORY"], os.environ["GITHUB_TOKEN"]
    number = os.environ.get("TRIAGE_ISSUE", "").strip()
    if number:  # by hand: an issue already there
        if not number.isdigit():
            print("TRIAGE_ISSUE must be a number")
            return 1
        issue = api("GET", f"/repos/{repo}/issues/{number}", token)
    else:
        event = json.load(open(os.environ["GITHUB_EVENT_PATH"]))
        issue = event["issue"]
    if issue.get("pull_request") or issue.get("user", {}).get("type") == "Bot":
        return 0
    # Only as it is opened: an edit, a reopening by the maintainer or a run by hand never closes it as a repeat.
    if not number and event.get("action") == "opened":
        login = issue.get("user", {}).get("login") or ""
        try:
            earlier = api("GET", f"/repos/{repo}/issues?creator={urllib.parse.quote(login)}&state=all&sort=created"
                                 "&direction=desc&per_page=50", token) if login else []
        except urllib.error.HTTPError as e:
            print(f"Warning: could not list the earlier issues: {e}")
            earlier = []
        first = duplicate_of(issue, earlier)
        if first:
            print(f"issue #{issue['number']}: repeats #{first}, closed as a duplicate")
            apply(repo, issue["number"], ["duplicate"], duplicate_comment(issue, first), token, close="duplicate")
            return 0
    try:
        latest = api("GET", f"/repos/{repo}/releases/latest", token)["tag_name"].lstrip("v")
    except urllib.error.HTTPError:
        latest = None
    labels, comment, should_close = triage(issue, rules, latest)
    print(f"issue #{issue['number']}: labels {labels}, comment {'yes' if comment else 'no'}, close: {should_close}")
    apply(repo, issue["number"], labels, comment, token, close=should_close)
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv))
