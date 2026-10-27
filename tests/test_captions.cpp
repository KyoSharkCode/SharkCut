#include "core/captions.h"
#include "core/edit.h"
#include "core/project_file.h"

#include <catch2/catch_test_macros.hpp>

#include <string>
#include <vector>

using namespace sharkcut;

namespace {

constexpr FrameRate k60{60, 1};

// Palabras de 0,3 s seguidas (0,1 s de pausa entre ellas), a partir de `startUs`.
std::vector<TimedWord> spoken(const std::vector<std::string>& texts, std::int64_t startUs = 0) {
    std::vector<TimedWord> words;
    std::int64_t t = startUs;
    for (const std::string& text : texts) {
        words.push_back({t, t + 300'000, text});
        t += 400'000;
    }
    return words;
}

std::vector<std::string> texts(const std::vector<Caption>& captions) {
    std::vector<std::string> result;
    for (const Caption& c : captions) {
        result.push_back(c.text);
    }
    return result;
}

void checkNoOverlap(const std::vector<Caption>& captions) {
    for (std::size_t i = 1; i < captions.size(); ++i) {
        CHECK(captions[i].start >= captions[i - 1].end());
    }
    for (const Caption& c : captions) {
        CHECK(c.length > 0);
    }
}

} // namespace

TEST_CASE("Captions split by characters per line and lines", "[captions]") {
    const auto words = spoken({"hola", "a", "todos", "bienvenidos", "al", "directo", "de", "hoy"});
    CaptionLayout layout;
    layout.maxChars = 12;
    layout.lines = CaptionLayout::Lines::Single;
    layout.minDurationUs = 0;
    auto captions = layoutCaptions(words, layout, k60);
    CHECK(texts(captions) ==
          std::vector<std::string>{"hola a todos", "bienvenidos", "al directo", "de hoy"});
    checkNoOverlap(captions);
    // Tramo: de la primera a la última palabra.
    CHECK(captions[0].start == 0);
    CHECK(captions[0].end() == 66); // 1,1 s
    REQUIRE(captions[0].words.size() == 3);
    CHECK(captions[0].words[2].text == "todos");
    CHECK(captions[0].words[2].start == 48);

    layout.lines = CaptionLayout::Lines::Double;
    captions = layoutCaptions(words, layout, k60);
    CHECK(texts(captions) ==
          std::vector<std::string>{"hola a todos\nbienvenidos", "al directo\nde hoy"});

    layout.lines = CaptionLayout::Lines::Word;
    captions = layoutCaptions(words, layout, k60);
    CHECK(captions.size() == words.size());
    checkNoOverlap(captions);
}

TEST_CASE("Captions end at sentences and pauses", "[captions]") {
    auto words = spoken({"Hola.", "Que", "tal", "estas?"});
    words.push_back({5'000'000, 5'300'000, "vale"}); // tras un silencio largo
    CaptionLayout layout;
    layout.minDurationUs = 0;
    const auto captions = layoutCaptions(words, layout, k60);
    CHECK(texts(captions) == std::vector<std::string>{"Hola.", "Que tal estas?", "vale"});

    layout.removePunctuation = true;
    CHECK(texts(layoutCaptions(words, layout, k60)) ==
          std::vector<std::string>{"Hola", "Que tal estas", "vale"});
    CHECK(withoutPunctuation("¿Qué? ¡Sí!, «vale»…") == "Qué Sí vale");
    CHECK(utf8Length("¿Qué?") == 5);
}

TEST_CASE("Caption minimum duration and gap never overlap the next one", "[captions]") {
    auto words = spoken({"uno."});
    const auto later = spoken({"dos."}, 1'000'000);
    words.insert(words.end(), later.begin(), later.end());
    CaptionLayout layout;
    layout.minDurationUs = 3'000'000;
    layout.gapFrames = 6;
    const auto captions = layoutCaptions(words, layout, k60);
    REQUIRE(captions.size() == 2);
    CHECK(captions[0].end() == 60 - 6); // la duración mínima cede ante el siguiente
    CHECK(captions[1].start == 60);
    CHECK(captions[1].length == 180); // el último sí dura lo mínimo
}

TEST_CASE("Caption tracks can be split again with other limits", "[captions]") {
    Project p;
    const std::size_t track = addCaptionTrack(p);
    CaptionLayout layout;
    layout.maxChars = 42;
    layout.minDurationUs = 0;
    const auto words = spoken({"esto", "es", "una", "prueba", "de", "subtitulos", "largos"});
    REQUIRE(setCaptions(p, track, layoutCaptions(words, layout, k60)));
    REQUIRE(p.captionTracks[track].captions.size() == 1);

    layout.maxChars = 10;
    layout.lines = CaptionLayout::Lines::Single;
    REQUIRE(relayoutCaptions(p, track, layout));
    CHECK(texts(p.captionTracks[track].captions) ==
          std::vector<std::string>{"esto es", "una prueba", "de", "subtitulos", "largos"});
    // Mantienen el tiempo de sus palabras.
    CHECK(p.captionTracks[track].captions[1].start == 48);

    // Un subtítulo escrito a mano reparte su tramo entre sus palabras.
    CaptionTrack manual;
    Caption c;
    c.start = 0;
    c.length = 60;
    c.text = "ab cd";
    manual.captions.push_back(c);
    const auto split = captionWords(manual, k60);
    REQUIRE(split.size() == 2);
    CHECK(split[0].startUs == 0);
    CHECK(split[1].startUs == 500'000);
    CHECK(split[1].endUs == 1'000'000);
}

TEST_CASE("Caption edits respect their neighbours", "[captions]") {
    Project p;
    const std::size_t track = addCaptionTrack(p);
    const ObjectId a = addCaption(p, track, 0, 60, "a");
    const ObjectId b = addCaption(p, track, 100, 60, "b");
    REQUIRE(a != 0);
    REQUIRE(b != 0);
    CHECK(addCaption(p, track, 30, 60, "dentro de a") == 0);
    // En el hueco: se recorta hasta el siguiente.
    const ObjectId c = addCaption(p, track, 70, 60, "c");
    REQUIRE(c != 0);
    CHECK(p.caption(c)->end() == 100);
    REQUIRE(removeCaptions(p, {c}));

    CHECK(moveCaption(p, b, 10)); // no pisa a "a"
    CHECK(p.caption(b)->start == 60);
    // Más allá de los vecinos se limita (aquí, a donde ya estaba: sin cambios).
    CHECK_FALSE(trimCaption(p, a, -5, 200));
    CHECK(trimCaption(p, a, 10, 50));
    CHECK(trimCaption(p, a, -5, 200));
    CHECK(p.caption(a)->start == 0);
    CHECK(p.caption(a)->end() == 60);

    CHECK(p.captionAt(track, 59)->id == a);
    CHECK(p.captionAt(track, 60)->id == b);
    CHECK(p.captionAt(track, 500) == nullptr);

    // Cambiar el texto descarta los tiempos de las palabras.
    REQUIRE(setCaptions(p, track, layoutCaptions(spoken({"hola", "mundo"}), {}, k60)));
    const ObjectId first = p.captionTracks[track].captions[0].id;
    CHECK_FALSE(p.caption(first)->words.empty());
    REQUIRE(setCaptionText(p, first, "hola gente"));
    CHECK(p.caption(first)->words.empty());

    // Estilo propio y de la pista.
    CaptionStyle big;
    big.size = 120;
    REQUIRE(setCaptionStyle(p, {first}, big));
    CHECK(p.styleOf(track, *p.caption(first)).size == 120);
    CaptionStyle trackStyle;
    trackStyle.color = 0xFFD800;
    REQUIRE(setCaptionTrackStyle(p, track, trackStyle, true));
    CHECK_FALSE(p.caption(first)->style.has_value());
    CHECK(p.styleOf(track, *p.caption(first)).color == 0xFFD800);

    // Pista bloqueada: nada cambia.
    REQUIRE(setCaptionTrackLocked(p, track, true));
    CHECK_FALSE(setCaptionText(p, first, "otra"));
    CHECK_FALSE(removeCaptions(p, {first}));
}

TEST_CASE("The blade splits captions by their words", "[captions]") {
    Project p;
    const std::size_t track = addCaptionTrack(p);
    // Transcrita: cada palabra va a la mitad donde cae su centro.
    REQUIRE(setCaptions(p, track, layoutCaptions(spoken({"uno", "dos", "tres", "cuatro"}), {}, k60)));
    REQUIRE(p.captionTracks[track].captions.size() == 1);
    const Caption whole = p.captionTracks[track].captions.front();
    REQUIRE(whole.text == "uno dos tres cuatro");
    CHECK_FALSE(splitCaptionsAt(p, whole.start)); // en su primer fotograma no se divide
    REQUIRE(splitCaptionsAt(p, whole.start + 45));
    const std::vector<Caption>& halves = p.captionTracks[track].captions;
    REQUIRE(halves.size() == 2);
    CHECK(texts(halves) == std::vector<std::string>{"uno dos", "tres cuatro"});
    CHECK(halves[0].end() == halves[1].start);
    CHECK(halves[1].end() == whole.end());
    REQUIRE(halves[1].words.size() == 2);
    CHECK(halves[1].words[0].text == "tres");
    CHECK(halves[1].words[0].start == 3); // 48 − 45: sigue en su sitio
    checkNoOverlap(halves);

    // Escrito a mano: el texto se reparte en proporción, con los saltos de línea.
    const ObjectId manual = addCaption(p, track, 1000, 100, "hola\na todos");
    REQUIRE(manual != 0);
    REQUIRE(splitCaptionsAt(p, 1030, {manual}));
    CHECK(p.caption(manual)->text == "hola");
    const Caption* rest = p.captionAt(track, 1030);
    REQUIRE(rest != nullptr);
    CHECK(rest->text == "a todos");
    CHECK(rest->length == 70);

    // Una sola palabra: las dos mitades la llevan.
    const ObjectId one = addCaption(p, track, 2000, 100, "¡Hola!");
    REQUIRE(splitCaptionsAt(p, 2050, {one}));
    CHECK(p.captionAt(track, 2000)->text == "¡Hola!");
    CHECK(p.captionAt(track, 2050)->text == "¡Hola!");

    // Bloqueada: nada.
    REQUIRE(setCaptionTrackLocked(p, track, true));
    CHECK_FALSE(splitCaptionsAt(p, 2020));
}

TEST_CASE("Dragging the edge between touching captions moves both", "[captions]") {
    Project p;
    const std::size_t track = addCaptionTrack(p);
    const ObjectId a = addCaption(p, track, 0, 60, "a");
    const ObjectId b = addCaption(p, track, 60, 60, "b");
    const ObjectId c = addCaption(p, track, 200, 60, "c");
    REQUIRE((a != 0 && b != 0 && c != 0));

    // Pegados: «a» se alarga y «b» se acorta, y al revés.
    CHECK(captionEdgeRoom(p, a).second == 119);
    REQUIRE(trimCaptionEdge(p, a, true, 90));
    CHECK(p.caption(a)->end() == 90);
    CHECK(p.caption(b)->start == 90);
    CHECK(p.caption(b)->end() == 120);
    REQUIRE(trimCaptionEdge(p, b, false, 30));
    CHECK(p.caption(a)->end() == 30);
    CHECK(p.caption(b)->start == 30);
    // Cada uno se queda con al menos un fotograma.
    REQUIRE(trimCaptionEdge(p, a, true, 500));
    CHECK(p.caption(b)->length == 1);
    CHECK(p.caption(a)->end() == 119);

    // Un borde libre se alarga hasta el hueco que haya.
    CHECK(captionEdgeRoom(p, b).second == 200);
    REQUIRE(trimCaptionEdge(p, b, true, 500));
    CHECK(p.caption(b)->end() == 200);
    // Y ahora «b» y «c» van pegados.
    CHECK(captionEdgeRoom(p, c).first == 120);
    REQUIRE(trimCaptionEdge(p, c, false, 150));
    CHECK(p.caption(b)->end() == 150);
    CHECK(p.caption(c)->start == 150);
    checkNoOverlap(p.captionTracks[track].captions);
}

TEST_CASE("Ripple edits move and cut captions", "[captions]") {
    Project p;
    const std::size_t track = addCaptionTrack(p);
    std::vector<Caption> list;
    // 0-60 "antes", 100-200 con palabras a 100-140 y 160-200, 300-360 "después".
    Caption before;
    before.start = 0;
    before.length = 60;
    before.text = "antes";
    Caption middle;
    middle.start = 100;
    middle.length = 100;
    middle.text = "uno dos";
    middle.words = {{0, 40, "uno"}, {60, 100, "dos"}};
    Caption after;
    after.start = 300;
    after.length = 60;
    after.text = "despues";
    REQUIRE(setCaptions(p, track, {before, middle, after}));

    // Se quita [140, 160) (entre las dos palabras) y lo que sigue se adelanta 20.
    REQUIRE(extractRange(p, 140, 160));
    const auto& captions = p.captionTracks[track].captions;
    REQUIRE(captions.size() == 3);
    CHECK(captions[1].start == 100);
    CHECK(captions[1].length == 80);
    CHECK(captions[1].text == "uno dos");
    CHECK(captions[1].words[1].start == 40);
    CHECK(captions[2].start == 280);

    // Quitar la primera palabra: queda "dos", empezando donde estaba el corte.
    REQUIRE(extractRange(p, 90, 140));
    CHECK(captions[1].start == 90);
    CHECK(captions[1].text == "dos");
    REQUIRE(captions[1].words.size() == 1);
    CHECK(captions[1].words[0].start == 0);

    // Un subtítulo entero dentro del tramo desaparece.
    REQUIRE(extractRange(p, 0, 60));
    CHECK(captions.front().text == "dos");
}

TEST_CASE("Srt files round trip", "[captions][srt]") {
    CaptionTrack track;
    Caption a;
    a.start = 60;
    a.length = 90;
    a.text = "Hola\nsegunda línea";
    track.captions.push_back(a);
    const std::string srt = captionsToSrt(track, k60);
    CHECK(srt == "1\n00:00:01,000 --> 00:00:02,500\nHola\nsegunda línea\n\n");

    const auto entries = parseSrt("\xEF\xBB\xBF"
                                  "1\r\n00:00:01,000 --> 00:00:02,500\r\nHola\r\n"
                                  "segunda línea\r\n\r\n2\r\n00:01:00.000 --> 00:01:01,000\r\n"
                                  "Otra\r\n\r\nbasura\r\n");
    REQUIRE(entries.size() == 2);
    CHECK(entries[0] == SrtEntry{1'000'000, 2'500'000, "Hola\nsegunda línea"});
    CHECK(entries[1].startUs == 60'000'000);
    const auto captions = captionsFromSrt(entries, k60);
    REQUIRE(captions.size() == 2);
    CHECK(captions[0].start == 60);
    CHECK(captions[0].length == 90);
    CHECK(captions[1].start == 3600);
}

TEST_CASE("Caption tracks survive a project round trip", "[captions][project]") {
    Project p;
    const std::size_t track = addCaptionTrack(p);
    REQUIRE(setCaptions(p, track, layoutCaptions(spoken({"hola", "mundo."}), {}, k60)));
    CaptionStyle own;
    own.font = "Impact";
    own.color = 0x12AB34;
    own.box = 0.5;
    REQUIRE(setCaptionStyle(p, {p.captionTracks[track].captions[0].id}, own));
    REQUIRE(setCaptionTrackHidden(p, track, true));

    const Project loaded = projectFromJson(projectToJson(p, {}), {});
    REQUIRE(loaded.captionTracks.size() == 1);
    CHECK(loaded.captionTracks == p.captionTracks);
}

TEST_CASE("Relayout never makes the sequence longer", "[captions]") {
    Project p;
    MediaItem item;
    item.path = "/medios/voz.mp4";
    item.durationUs = 2'000'000;
    item.hasVideo = true;
    item.hasAudio = true;
    placeClip(p, addMedia(p, item), 0, 0, 120); // 2 s
    const std::size_t track = addCaptionTrack(p);
    REQUIRE(setCaptions(p, track, layoutCaptions(spoken({"hola", "mundo."}), {}, k60)));
    CaptionLayout layout;
    layout.lines = CaptionLayout::Lines::Word;
    layout.minDurationUs = 3'000'000;
    REQUIRE(relayoutCaptions(p, track, layout));
    CHECK(p.captionTracks[track].captions.back().end() == 120);
    CHECK(p.length() == 120);
}
