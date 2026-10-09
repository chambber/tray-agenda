#include "../tray-agenda.wh.cpp"
#include <cstdio>

static int g_fail = 0;
#define CHECK(cond) do { if (!(cond)) { std::printf("FAIL line %d: %s\n", __LINE__, #cond); ++g_fail; } } while (0)

using namespace winrt;

static AgendaEntry Ev(const wchar_t* title, int64_t start, int64_t end, bool ooo = false,
                      AgendaEntry::ResponseState rs = AgendaEntry::ResponseState::Accepted) {
    AgendaEntry e;
    e.title = title; e.source = L"Work"; e.startUnix = start; e.endUnix = end;
    e.outOfOffice = ooo; e.responseState = rs;
    return e;
}

int main() {
    init_apartment(apartment_type::multi_threaded);
    // --- time parsing
    int64_t t = 0;
    CHECK(ParseRfc3339(L"2026-10-09T14:00:00-03:00", &t) && t == 1791565200LL - 0);
    CHECK(ParseRfc3339(L"2026-10-09T17:00:00Z", &t) && t == 1791565200LL);
    CHECK(ParseRfc3339(L"2026-10-09T17:00:00.250+00:00", &t) && t == 1791565200LL);
    CHECK(!ParseRfc3339(L"2026-10-09T17:00:00", &t));
    CHECK(!ParseRfc3339(L"2026-13-09T17:00:00Z", &t));
    CHECK(FormatRfc3339Utc(1791565200LL) == L"2026-10-09T17:00:00Z");
    CHECK(FormatRfc3339Utc(0) == L"1970-01-01T00:00:00Z");
    // --- crypto / encoding
    unsigned char d[32];
    CHECK(Sha256("abc", d) && Base64UrlEncode(d, 32) == "ungWv48Bz-pBQUDeXa4iI7ADYaOWF3qctBD_YfIAFa0");
    CHECK(Sha256("dBjftJeZ4CVP-mB92K27uhbUJU1p1r_wW1gFWFOEjXk", d) &&
          Base64UrlEncode(d, 32) == "E9Melhoa2OwvFrEMTJguCHaoeK1t8URWbuGJSstw-cM");
    CHECK(UrlEncode("a b&c=d/\xC3\xA9") == "a%20b%26c%3Dd%2F%C3%A9");
    CHECK(UrlDecode("a%20b+c%26") == "a b c&");
    unsigned char r1[32], r2[32];
    CHECK(RandomBytes(r1, 32) && RandomBytes(r2, 32) && memcmp(r1, r2, 32) != 0);
    // --- DPAPI roundtrip
    std::string blob, plain;
    CHECK(DpapiProtect("refresh-token-xyz", &blob) && blob.find("refresh-token") == std::string::npos);
    CHECK(DpapiUnprotect(blob, &plain) && plain == "refresh-token-xyz");
    // --- meeting links
    CHECK(MeetCodeFromUrl(L"https://meet.google.com/abc-defg-hij?authuser=0") == L"abc-defg-hij");
    CHECK(MeetCodeFromUrl(L"https://meet.google.com/lookup/abc").empty());
    CHECK(MeetCodeFromUrl(L"http://meet.google.com/abc-defg-hij").empty());
    std::wstring p, u;
    CHECK(CanonicalZoomTeamsUrl(L"https://us02web.zoom.us/j/123456789?pwd=AbC123.", &p, &u) && p == L"zoom" && u == L"https://us02web.zoom.us/j/123456789?pwd=AbC123");
    CHECK(CanonicalZoomTeamsUrl(L"https://Teams.Microsoft.com/l/meetup-join/19%3ameeting_abc/0?context=%7b%7d", &p, &u) && p == L"teams");
    CHECK(CanonicalZoomTeamsUrl(L"https://aka.ms/jointeamsmeeting?omkt=pt-BR", &p, &u) && u == L"https://aka.ms/JoinTeamsMeeting?omkt=pt-BR");
    CHECK(!CanonicalZoomTeamsUrl(L"https://evil.com/j/123456789", &p, &u));
    CHECK(!CanonicalZoomTeamsUrl(L"https://zoom.us.evil.com/j/123456789", &p, &u));
    CHECK(!CanonicalZoomTeamsUrl(L"http://zoom.us/j/123456789", &p, &u));
    std::wstring mc, pr, ur;
    ScanMeetingLinks({L"Join: https://us02web.zoom.us/j/987654321?pwd=x and more", L"https://meet.google.com/aaa-bbbb-ccc"}, &mc, &pr, &ur);
    CHECK(mc == L"aaa-bbbb-ccc" && pr == L"zoom" && ur == L"https://us02web.zoom.us/j/987654321?pwd=x");
    // --- selection
    const int64_t T = 1791565200LL, lead = 600;
    {
        std::vector<AgendaEntry> a = {Ev(L"Running", T - 3600, T + 3600), Ev(L"Next", T + 500, T + 4000)};
        AgendaEntry out;
        CHECK(SelectWidgetEntry(a, T, lead, &out) && out.title == L"Next");          // within lead: takes over
        CHECK(SelectWidgetEntry(a, T - 200, lead, &out) && out.title == L"Running"); // 700s away: no
    }
    {
        std::vector<AgendaEntry> a = {Ev(L"Older", T - 3600, T + 3600), Ev(L"Newer", T - 600, T + 600)};
        AgendaEntry out;
        CHECK(SelectWidgetEntry(a, T, lead, &out) && out.title == L"Newer");
        CHECK(SelectWidgetEntry(a, T + 700, lead, &out) && out.title == L"Older");
    }
    {
        std::vector<AgendaEntry> a = {Ev(L"Away", T - 3600, T + 7200, true), Ev(L"Standup", T - 600, T + 600)};
        AgendaEntry out;
        CHECK(SelectWidgetEntry(a, T, lead, &out) && out.title == L"Standup");
        CHECK(SelectWidgetEntry(a, T + 3600, lead, &out) && out.title == L"Away");
        std::vector<AgendaEntry> only = {Ev(L"Away", T - 3600, T + 7200, true)};
        CHECK(SelectWidgetEntry(only, T, lead, &out) && out.title == L"Away");
    }
    {
        std::vector<AgendaEntry> a = {Ev(L"Far", T + 1800, T + 3600)};
        AgendaEntry out;
        CHECK(SelectWidgetEntry(a, T, lead, &out) && out.title == L"Far");   // preview window 1h
        CHECK(!SelectWidgetEntry(a, T - 3600, lead, &out));
    }
    {
        // same start: regular before out-of-office
        std::vector<AgendaEntry> ev = {Ev(L"Away", T + 100, T + 200, true), Ev(L"Standup", T + 100, T + 200)};
        auto agenda = BuildAgenda(ev, T);
        CHECK(agenda.size() == 2 && agenda[0].title == L"Standup");
    }
    // --- reminders
    {
        ModSettings s; s.notify_lead_minutes = 10;
        g_notified.clear();
        std::vector<AgendaEntry> ev = {Ev(L"Standup", T + 600, T + 1800)};
        CHECK(DueToasts(ev, T - 1, s).empty());
        auto d1 = DueToasts(ev, T, s);
        CHECK(d1.size() == 1 && d1[0].body.rfind(L"Starts in 10 min", 0) == 0);
        g_notified[d1[0].key] = T;
        CHECK(DueToasts(ev, T + 5, s).empty());
        auto d2 = DueToasts(ev, T + 600, s);
        CHECK(d2.size() == 1 && d2[0].body.rfind(L"Starting now", 0) == 0);
        CHECK(DueToasts(ev, T + 600 + 120, s).empty());
        // only accepted, timed, non-OOO
        std::vector<AgendaEntry> bad = {Ev(L"A", T + 60, T + 900, false, AgendaEntry::ResponseState::NeedsResponse),
                                         Ev(L"B", T + 60, T + 900, true), Ev(L"C", T + 60, T + 900, false, AgendaEntry::ResponseState::Declined)};
        bad.push_back(Ev(L"D", T + 60, T + 900)); bad.back().allDay = true;
        CHECK(DueToasts(bad, T, s).empty());
        CHECK(NextNotificationBoundary(s, ev, T) == 600);
        CHECK(NextNotificationBoundary(s, ev, T + 100) == 500);
    }
    // --- Google event JSON
    {
        const char* json = R"({"status":"confirmed","eventType":"default","summary":"Sprint planning","location":"Sala 2",
          "description":"Join https://us02web.zoom.us/j/123456789?pwd=abc please",
          "hangoutLink":"https://meet.google.com/abc-defg-hij",
          "attendees":[{"self":true,"responseStatus":"needsAction"}],
          "start":{"dateTime":"2026-10-09T14:00:00-03:00"},"end":{"dateTime":"2026-10-09T15:00:00-03:00"}})";
        JsonObject o{nullptr};
        CHECK(ParseJsonObject(json, &o));
        AgendaEntry e;
        CHECK(GoogleItemToEntry(o, L"Work", L"cal", T, &e));
        CHECK(e.title == L"Sprint planning" && e.startUnix == T && e.endUnix == T + 3600);
        CHECK(e.googleMeetCode == L"abc-defg-hij" && e.meetingUrl.empty());   // Meet wins
        CHECK(e.responseState == AgendaEntry::ResponseState::NeedsResponse && !e.outOfOffice);
        const char* ooo = R"({"eventType":"outOfOffice","summary":"Ferias","start":{"date":"2026-10-09"},"end":{"date":"2026-10-10"}})";
        JsonObject o2{nullptr};
        CHECK(ParseJsonObject(ooo, &o2) && GoogleItemToEntry(o2, L"Work", L"cal", T, &e));
        CHECK(e.allDay && e.outOfOffice && e.endUnix - e.startUnix >= 23 * 3600);
        const char* cancelled = R"({"status":"cancelled","summary":"x","start":{"dateTime":"2026-10-09T14:00:00Z"}})";
        JsonObject o3{nullptr};
        CHECK(ParseJsonObject(cancelled, &o3) && !GoogleItemToEntry(o3, L"Work", L"cal", T, &e));
        const char* zoom = R"({"summary":"z","location":"https://evil.example/j/1","description":"https://us02web.zoom.us/j/123456789?pwd=abc",
          "start":{"dateTime":"2026-10-09T14:00:00Z"},"end":{"dateTime":"2026-10-09T15:00:00Z"},
          "attendees":[{"self":true,"responseStatus":"accepted"},{"responseStatus":"declined"}]})";
        JsonObject o4{nullptr};
        CHECK(ParseJsonObject(zoom, &o4) && GoogleItemToEntry(o4, L"Work", L"cal", T, &e));
        CHECK(e.meetingProvider == L"zoom" && e.googleMeetCode.empty() && e.responseState == AgendaEntry::ResponseState::Accepted);
    }

    // --- own events without guests, declined events
    {
        const char* own = R"({"summary":"Focus","organizer":{"self":true},"start":{"dateTime":"2026-10-09T14:00:00-03:00"},"end":{"dateTime":"2026-10-09T15:00:00-03:00"}})";
        const char* other = R"({"summary":"Team holiday party","organizer":{"self":false},"start":{"dateTime":"2026-10-09T14:00:00-03:00"},"end":{"dateTime":"2026-10-09T15:00:00-03:00"}})";
        JsonObject o1{nullptr}, o2{nullptr};
        AgendaEntry e1, e2;
        CHECK(ParseJsonObject(own, &o1) && GoogleItemToEntry(o1, L"Work", L"cal", T, &e1));
        CHECK(e1.responseState == AgendaEntry::ResponseState::Accepted);
        CHECK(ParseJsonObject(other, &o2) && GoogleItemToEntry(o2, L"Shared", L"cal", T, &e2));
        CHECK(e2.responseState == AgendaEntry::ResponseState::Neutral);
        std::vector<AgendaEntry> a = {Ev(L"Declined", T, T + 3600, false, AgendaEntry::ResponseState::Declined), Ev(L"Kept", T - 600, T + 3600)};
        AgendaEntry out;
        CHECK(SelectWidgetEntry(a, T, 600, &out) && out.title == L"Kept");
        std::vector<AgendaEntry> only = {Ev(L"Declined", T, T + 3600, false, AgendaEntry::ResponseState::Declined)};
        CHECK(!SelectWidgetEntry(only, T, 600, &out));
    }
    // --- ICS feeds
    {
        auto ics = [](const std::string& body) {
            return "BEGIN:VCALENDAR\r\nVERSION:2.0\r\n" + body + "END:VCALENDAR\r\n";
        };
        auto parse = [&](const std::string& body) { return ParseIcsFeed(ics(body), L"Feed", T); };
        auto ev = [](const std::string& body) { return "BEGIN:VEVENT\r\nUID:u1\r\nSUMMARY:Evt\r\n" + body + "END:VEVENT\r\n"; };

        auto r = parse(ev("DTSTART:20261009T170000Z\r\nDTEND:20261009T180000Z\r\n"));
        CHECK(r.failure == FetchFailure::None && r.events.size() == 1);
        CHECK(r.events[0].startUnix == T && r.events[0].endUnix == T + 3600 && r.events[0].fromIcs && !r.events[0].allDay);
        // time zones: IANA and Windows names
        r = parse(ev("DTSTART;TZID=America/Sao_Paulo:20261009T140000\r\nDTEND;TZID=America/Sao_Paulo:20261009T150000\r\n"));
        CHECK(r.events.size() == 1 && r.events[0].startUnix == T);
        r = parse(ev("DTSTART;TZID=Eastern Standard Time:20261009T140000\r\nDURATION:PT30M\r\n"));
        CHECK(r.events.size() == 1 && r.events[0].startUnix == T + 3600 && r.events[0].endUnix == T + 3600 + 1800);
        r = parse(ev("DTSTART;TZID=Europe/London:20261009T180000\r\nDTEND;TZID=Europe/London:20261009T190000\r\n"));  // BST: 17:00Z
        CHECK(r.events.size() == 1 && r.events[0].startUnix == T);
        // weekly recurrence with EXDATE
        const std::string weekly = "DTSTART:20260101T170000Z\r\nDTEND:20260101T180000Z\r\nRRULE:FREQ=WEEKLY;BYDAY=TH,FR\r\n";
        r = parse(ev(weekly));
        CHECK(r.events.size() == 2);  // Thu Oct 8 and Fri Oct 9 17:00Z
        r = parse(ev(weekly + "EXDATE:20261008T170000Z\r\n"));
        CHECK(r.events.size() == 1 && r.events[0].startUnix == T);
        r = parse(ev("DTSTART:20260101T170000Z\r\nDTEND:20260101T180000Z\r\nRRULE:FREQ=WEEKLY;BYDAY=TH,FR;UNTIL=20261008T235959Z\r\n"));
        CHECK(r.events.size() == 1 && r.events[0].startUnix == T - 86400);
        r = parse(ev("DTSTART:20260101T170000Z\r\nDTEND:20260101T180000Z\r\nRRULE:FREQ=WEEKLY;BYDAY=TH,FR;COUNT=3\r\n"));
        CHECK(r.events.empty());  // COUNT exhausted in January
        // monthly "2nd Friday", daily COUNT, interval
        r = parse(ev("DTSTART:20260109T170000Z\r\nDTEND:20260109T180000Z\r\nRRULE:FREQ=MONTHLY;BYDAY=2FR\r\n"));
        CHECK(r.events.size() == 1 && r.events[0].startUnix == T);
        r = parse(ev("DTSTART:20261008T170000Z\r\nDTEND:20261008T180000Z\r\nRRULE:FREQ=DAILY;COUNT=3\r\n"));
        CHECK(r.events.size() == 3);
        r = parse(ev("DTSTART:20260101T170000Z\r\nDTEND:20260101T180000Z\r\nRRULE:FREQ=WEEKLY;INTERVAL=2;BYDAY=TH\r\n"));
        CHECK(r.events.size() == 1 && r.events[0].startUnix == T - 86400);  // every 2nd Thursday: Oct 8
        r = parse(ev("DTSTART:20250109T170000Z\r\nDTEND:20250109T180000Z\r\nRRULE:FREQ=YEARLY\r\n"));
        CHECK(r.events.empty());  // Jan 9 each year
        r = parse(ev("DTSTART:20250409T170000Z\r\nDTEND:20250409T180000Z\r\nRRULE:FREQ=YEARLY;BYMONTH=10;BYMONTHDAY=9\r\n"));
        CHECK(r.events.size() == 1 && r.events[0].startUnix == T);  // yearly on Oct 9
        // override (RECURRENCE-ID) moves one occurrence; cancelled ones disappear
        std::string master = "BEGIN:VEVENT\r\nUID:m1\r\nSUMMARY:Series\r\nDTSTART:20260105T170000Z\r\nDTEND:20260105T180000Z\r\nRRULE:FREQ=WEEKLY;BYDAY=FR\r\nEND:VEVENT\r\n";
        std::string moved = "BEGIN:VEVENT\r\nUID:m1\r\nSUMMARY:Series (moved)\r\nRECURRENCE-ID:20261009T170000Z\r\nDTSTART:20261009T190000Z\r\nDTEND:20261009T200000Z\r\nEND:VEVENT\r\n";
        r = ParseIcsFeed(ics(master + moved), L"Feed", T);
        CHECK(r.events.size() == 1 && r.events[0].startUnix == T + 7200 && r.events[0].title == L"Series (moved)");
        std::string cancelled = "BEGIN:VEVENT\r\nUID:m1\r\nSUMMARY:x\r\nRECURRENCE-ID:20261009T170000Z\r\nDTSTART:20261009T170000Z\r\nSTATUS:CANCELLED\r\nEND:VEVENT\r\n";
        r = ParseIcsFeed(ics(master + cancelled), L"Feed", T);
        CHECK(r.events.empty());
        // all-day, folding, escapes, meeting links
        r = parse(ev("DTSTART;VALUE=DATE:20261009\r\nDTEND;VALUE=DATE:20261010\r\n"));
        CHECK(r.events.size() == 1 && r.events[0].allDay && r.events[0].endUnix - r.events[0].startUnix >= 23 * 3600);
        r = parse("BEGIN:VEVENT\r\nUID:z\r\nSUMMARY:Planning\\, Q4 \r\n kickoff\r\nDESCRIPTION:Join https://us02web.zoom.us/j/123456789?pwd=abc\\nthanks\r\nDTSTART:20261009T170000Z\r\nDTEND:20261009T180000Z\r\nEND:VEVENT\r\n");
        CHECK(r.events.size() == 1 && r.events[0].title == L"Planning, Q4 kickoff" && r.events[0].meetingProvider == L"zoom");
        r = parse(ev("STATUS:CANCELLED\r\nDTSTART:20261009T170000Z\r\nDTEND:20261009T180000Z\r\n"));
        CHECK(r.events.empty());
        CHECK(ParseIcsFeed("<html>nope</html>", L"Feed", T).failure == FetchFailure::Config);
        // far-future / far-past events are outside the window
        r = parse(ev("DTSTART:20261201T170000Z\r\nDTEND:20261201T180000Z\r\n"));
        CHECK(r.events.empty());
        // feed URLs
        std::wstring host, path;
        CHECK(SplitFeedUrl(L"webcal://Calendar.Example.com/a/b.ics?x=1", &host, &path) && host == L"calendar.example.com" && path == L"/a/b.ics?x=1");
        CHECK(!SplitFeedUrl(L"http://example.com/a.ics", &host, &path));
        CHECK(!SplitFeedUrl(L"https://user:pw@example.com/a.ics", &host, &path));
        CHECK(!SplitFeedUrl(L"https://example.com:8443/a.ics", &host, &path));
        // dedupe: same meeting in two sources keeps the accepted copy
        AgendaEntry a1 = Ev(L"Sync", T, T + 3600, false, AgendaEntry::ResponseState::NeedsResponse); a1.dedupKey = L"uid|1";
        AgendaEntry a2 = Ev(L"Sync", T, T + 3600, false, AgendaEntry::ResponseState::Accepted); a2.dedupKey = L"uid|1";
        AgendaEntry a3 = Ev(L"Other", T, T + 3600); a3.dedupKey = L"uid2|1";
        auto dd = DedupeEvents({a1, a2, a3});
        CHECK(dd.size() == 2 && dd[0].responseState == AgendaEntry::ResponseState::Accepted);
        // reminders for ICS events are opt-in via the setting
        ModSettings is; is.notify_lead_minutes = 10;
        AgendaEntry fe = Ev(L"Feed event", T + 60, T + 900, false, AgendaEntry::ResponseState::Neutral); fe.fromIcs = true;
        g_notified.clear();
        is.ics_notifications = true;
        CHECK(DueToasts({fe}, T, is).size() == 1);
        is.ics_notifications = false;
        CHECK(DueToasts({fe}, T, is).empty());
    }
    std::printf(g_fail ? "FAILED: %d\n" : "ALL PASSED\n", g_fail);
    return g_fail ? 1 : 0;
}
