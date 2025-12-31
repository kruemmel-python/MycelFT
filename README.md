# MycelFT  
**Context-Bound Anti-Exfiltration File Protection**

MycelFT ist **kein klassisches Verschlüsselungstool**, sondern eine **kontextgebundene Sicherheitsgrenze** auf Dateiebene.  
Das zentrale Designziel lautet:

> **Daten sollen ihren Wert verlieren, sobald sie ihren legitimen Kontext verlassen.**

MycelFT bindet Dateien bewusst an ihren Entstehungs- und Nutzungs­kontext (z. B. Datenträger, Dateiname, Umgebung).  
Außerhalb dieses Kontexts sind Daten **absichtlich nicht mehr rekonstruierbar**.

---

## Kernidee

- **Verschlüsselung:** deterministischer, kontextgebundener Keystream (XOR)
- **Integrität:** HMAC-SHA256 über den **Chiffrat-Zustand**
- **Schlüsselmodell:** kein Passwort, kein Master-Key, kein Recovery
- **Speicherung:**  
  - NTFS: Alternate Data Stream (ADS)  
  - andere Dateisysteme: Sidecar (`.mycelft.hmac`)
- **UX-Modell:** ein Toggle (`Encrypt/Decrypt`), kein Moduswechsel

---

## Was MycelFT garantiert

- Gestohlene oder kopierte Dateien sind **wertlos**
- Manipulierte Dateien werden **zuverlässig erkannt**
- Umbenennen oder Kontextwechsel kann **dauerhafte Unlesbarkeit** erzeugen
- Kein Schlüsselmaterial existiert außerhalb des Kontextes
- Kein Offline-Angriff auf exportierte Daten möglich

---

## Was MycelFT **absichtlich nicht kann**

Diese Einschränkungen sind **bewusste Designentscheidungen**, keine fehlenden Features.

### ❌ Kein Backup- oder Archivformat
- Keine portable Verschlüsselung
- Keine „sichere Datei zum Mitnehmen“
- Keine Wiederherstellung auf anderen Systemen

**Begründung:**  
Portabilität widerspricht dem Ziel „Exfiltration = wertlos“.

---

### ❌ Keine Recovery- oder Passwort-Funktion
- Kein Passwort
- Kein Master-Key
- Kein Reset
- Kein „Support-Key“

**Begründung:**  
Recovery-Mechanismen sind immer ein zusätzlicher Angriffsvektor.

---

### ❌ Kein Sync-, Cloud- oder Sharing-System
- Kein Geräte-übergreifender Zugriff
- Kein sicherer Cloud-Workflow
- Kein Multi-User-Sharing

**Begründung:**  
Synchronisation erfordert transportierbare Schlüssel – MycelFT verweigert genau das.

---

### ❌ Keine Full-Disk- oder Volume-Verschlüsselung
- Kein Ersatz für BitLocker, LUKS o. Ä.
- Keine OS- oder Boot-Integration
- Metadaten bleiben sichtbar

**Begründung:**  
MycelFT ist **datei- und workflow-zentriert**, nicht system-zentriert.

---

### ❌ Kein forensisches Compliance-Versprechen
- Keine FIPS-Zertifizierung
- Kein Anspruch auf formale Standard-Krypto-Beweise
- Kein „Enterprise Compliance Label“

**Begründung:**  
MycelFT folgt einem **anderen Sicherheitsparadigma**:  
Kontext statt Key-Management.

---

### ❌ Kein Schutz gegen laufende Systemkompromittierung
- Kein Schutz vor Malware während aktiver Nutzung
- Kein Schutz gegen RAM-Dump, Keylogging, Screenshots
- Kein Schutz bei kompromittierter User-Session

**Begründung:**  
MycelFT schützt **Daten im Ruhezustand** und bei **Exfiltration**, nicht bei Live-Angriffen.

---

### ❌ Rename / Copy kann Daten dauerhaft zerstören
- Umbenennen oder Verschieben kann zur Unlesbarkeit führen
- Das ist **kein Bug**, sondern ein Feature

**Begründung:**  
Der Dateikontext ist Teil des Sicherheitsmodells.

---

## Threat Model (kurz)

**MycelFT schützt gegen:**
- Datenabfluss durch Kopieren
- Diebstahl von Datenträgern
- Cloud-Leaks
- Offline-Analyse
- Manipulation von gespeicherten Dateien

**MycelFT schützt nicht gegen:**
- Angreifer mit aktiver Systemkontrolle
- Benutzerfehler außerhalb des definierten Workflows
- Komfort- oder Verfügbarkeitsanforderungen

---

## Philosophie

MycelFT verfolgt **Security by Invalidation** statt **Security by Recovery**.

> Wenn Daten den Kontext verlassen, sollen sie nicht „schwer zugänglich“,  
> sondern **wertlos** sein.

Das System ist bewusst kompromisslos – zugunsten klarer Sicherheitsgrenzen.

---

## Zielgruppe

MycelFT richtet sich an Anwender und Entwickler, die:

- Exfiltration verhindern wollen statt sie nur zu erschweren
- auf Recovery-Komfort bewusst verzichten
- Sicherheitsinvarianten höher gewichten als Benutzerfreundlichkeit
- Sicherheit als **Systemeigenschaft** verstehen

---

## Status

- Explorer-Tool (User-Mode): ✔️  
- Integrität (HMAC): ✔️  
- ADS + Sidecar-Fallback: ✔️  
- Kernel-Integration (Minifilter): geplant

---

**MycelFT ist kein Universalwerkzeug.  
Es ist eine klare Sicherheitsgrenze.**
