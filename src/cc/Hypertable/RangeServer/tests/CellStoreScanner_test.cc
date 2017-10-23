/*
 * Copyright (C) 2007-2016 Hypertable, Inc.
 *
 * This file is part of Hypertable.
 *
 * Hypertable is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; either version 3
 * of the License, or any later version.
 *
 * Hypertable is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA
 * 02110-1301, USA.
 */

#include <Common/Compat.h>

#include "../CellStoreFactory.h"
#include "../CellStoreV7.h"
#include "../Global.h"

#include <Hypertable/Lib/Key.h>
#include <Hypertable/Lib/Schema.h>
#include <Hypertable/Lib/SerializedKey.h>

#include <FsBroker/Lib/Client.h>

#include <AsyncComm/ConnectionManager.h>

#include <Common/Config.h>
#include <Common/Init.h>
#include <Common/DynamicBuffer.h>
#include <Common/FileUtils.h>
#include <Common/InetAddr.h>
#include <Common/System.h>
#include <Common/Usage.h>

#include <cstdlib>
#include <iostream>
#include <fstream>

using namespace Hypertable;
using namespace std;

namespace {
  const char *usage[] = {
    "usage: CellStoreScanner_test",
    "",
    "  This program tests for the proper functioning of the CellStore",
    "  scanner.  It creates a dummy cell store and then repeatedly scans",
    "  it with different ranges",
    (const char *)0
  };
  const char *schema_str =
  "<Schema>\n"
  "  <AccessGroup name=\"default\">\n"
  "    <ColumnFamily id=\"1\">\n"
  "      <Name>tag</Name>\n"
  "    </ColumnFamily>\n"
  "    <ColumnFamily id=\"2\">\n"
  "      <Name>foo</Name>\n"
  "    </ColumnFamily>\n"
  "  </AccessGroup>\n"
  "</Schema>";


  const char *schema2_str =
    "<Schema>\n"
    "  <AccessGroup name=\"default\">\n"
    "    <ColumnFamily id=\"1\">\n"
    "      <Name>a</Name>\n"
    "      <deleted>false</deleted>\n"
    "    </ColumnFamily>\n"
    "    <ColumnFamily id=\"2\">\n"
    "      <Name>b</Name>\n"
    "      <ttl>18000</ttl>\n"
    "      <deleted>false</deleted>\n"
    "    </ColumnFamily>\n"
    "    <ColumnFamily id=\"3\">\n"
    "      <Name>c</Name>\n"
    "      <deleted>false</deleted>\n"
    "    </ColumnFamily>\n"
    "  </AccessGroup>\n"
    "</Schema>";

  const char *words[] = {
    "Libra",
    "prolificity",
    "yonside",
    "testamentarily",
    "hypostasize",
    "scagliola",
    "bandyman",
    "proteinic",
    "paleothermic",
    "heteronereis",
    "undelightfully",
    "Megachilidae",
    "heparinize",
    "salamandrine",
    "stereospondylous",
    "favorableness",
    "aerolitic",
    "ovopyriform",
    "utick",
    "frike",
    "persecutor",
    "fondlesome",
    "Diplodocus",
    "federalism",
    "folkfree",
    "doorlike",
    "paeon",
    "healingly",
    "rugosity",
    "underhorsed",
    "Troic",
    "consummatively",
    "overleap",
    "cuminseed",
    "Hannibal",
    "etiologically",
    "unproperness",
    "Juliana",
    "unsnarl",
    "tarrily",
    "brattie",
    "tettigoniid",
    "garibaldi",
    "spermocarp",
    "palmicolous",
    "Laurencia",
    "basipodite",
    "peculiar",
    "Gulo",
    "heteronymously",
    "undersparred",
    "untoothsome",
    "proprovost",
    "luke",
    "ostracon",
    "Felapton",
    "allotypical",
    "Macrura",
    "verdelho",
    "semicircled",
    "humbug",
    "tenty",
    "Eastertide",
    "epacrid",
    "holoptychiid",
    "unguidedly",
    "Nummulites",
    "fondak",
    "protogine",
    "micromeasurement",
    "mahua",
    "blighting",
    "Guauaenok",
    "bradyacousia",
    "typhomania",
    "ducato",
    "trapezoid",
    "vipresident",
    "coiling",
    "depancreatization",
    "gnathotheca",
    "Balak",
    "micasize",
    "garnetwork",
    "thrack",
    "humoralist",
    "Philistia",
    "tropic",
    "cloysome",
    "oxypropionic",
    "upholstress",
    "therolatry",
    "seaworthiness",
    "nigrification",
    "Calystegia",
    "bulimiform",
    "leptotene",
    "protium",
    "schreinerize",
    "laemodipod",
    "Melastomaceae",
    "nonaluminous",
    "nonsense",
    "chatoyancy",
    "Pomacentrus",
    "kasm",
    "imploringness",
    "syngenesis",
    "infidelly",
    "opianyl",
    "ara",
    "architecturally",
    "unwifelike",
    "nonthinker",
    "Hun",
    "catacorolla",
    "dispulp",
    "formalesque",
    "afterstretch",
    "indicate",
    "turncap",
    "feldspathoid",
    "edea",
    "uralitic",
    "suidian",
    "hydrologist",
    "baar",
    "parapophysial",
    "hoin",
    "cavernoma",
    "ammonion",
    "coly",
    "propitiation",
    "overanalyze",
    "fringent",
    "planetless",
    "ganglia",
    "rehandler",
    "lucidness",
    "anthracyl",
    "Palaeophis",
    "murgavi",
    "Billjim",
    "thinglike",
    "hundredpenny",
    "spherulitic",
    "worserment",
    "phallitis",
    "blaeness",
    "Achernar",
    "Acroceraunian",
    "foreseeable",
    "phytooecology",
    "palaeostracan",
    "enfuddle",
    "Laemodipoda",
    "urolithology",
    "presynaptic",
    "harmonograph",
    "preadequate",
    "bucketful",
    "redoubtableness",
    "submaximal",
    "sociogenesis",
    "epididymodeferential",
    "capsulitis",
    "intercommunicability",
    "understander",
    "chrysomonadine",
    "couseranite",
    "untransposed",
    "argilliferous",
    "Alkoranic",
    "antisynod",
    "undisplaced",
    "indirectness",
    "clashy",
    "uncommitted",
    "glaieul",
    "electromagnetist",
    "underedge",
    "pretenseful",
    "tuberculously",
    "teratoma",
    "documental",
    "Manchester",
    "faciend",
    "cum",
    "lethologica",
    "usitate",
    "Coix",
    "othelcosis",
    "primogenitive",
    "whack",
    "gypsography",
    "earthboard",
    "kempt",
    "backstop",
    "clouding",
    "uncouched",
    "Panayano",
    "brig",
    "utriculosaccular",
    "personalist",
    "medioventral",
    "transcendentalist",
    "acetabuliform",
    "polycyanide",
    "skunkdom",
    "prepreference",
    "delftware",
    "Acanthodini",
    "frostproofing",
    "blackguardry",
    "tetrazane",
    "Olympianism",
    "type",
    "lubrify",
    "B",
    "grazable",
    "Schistosoma",
    "diversifoliate",
    "unconciliable",
    "infra",
    "nonvenous",
    "overbloom",
    "vivisectionally",
    "visualize",
    "postintestinal",
    "catachrestical",
    "tribalist",
    "digynian",
    "outhorror",
    "Tachina",
    "Syriologist",
    "historiette",
    "Amoreuxia",
    "coachmaking",
    "absenter",
    "stibic",
    "subsequence",
    "nonoecumenic",
    "caracolite",
    "Caripuna",
    "Uranicentric",
    "fletcher",
    "acediamine",
    "temerariously",
    "beadlet",
    "bancal",
    "mordication",
    "superdevilish",
    "Polynemus",
    "Interlingua",
    "diaphonia",
    "continentalist",
    "wastement",
    "cubelet",
    "Chordaceae",
    "unsatiableness",
    "prefinal",
    "Amanda",
    "Microsthenes",
    "nonrepetition",
    "corpus",
    "precast",
    "uncrib",
    "delayful",
    "carlings",
    "dilogy",
    "gravimetry",
    "unstoppable",
    "shoreman",
    "speakless",
    "inenarrable",
    "antiholiday",
    "benzolize",
    "inopportunely",
    "decrustation",
    "compositively",
    "Stenopelmatidae",
    "Rhapis",
    "younglet",
    "laeotropism",
    "cannonproof",
    "nonuterine",
    "phyllocyanic",
    "joist",
    "ascidiate",
    "berate",
    "holoquinonoid",
    "analeptical",
    "kynurine",
    "convert",
    "overfavorable",
    "pigflower",
    "suprapubic",
    "Mattapony",
    "citable",
    "urocerid",
    "alternately",
    "Suboscines",
    "dietetically",
    "spiffing",
    "pericarp",
    "placental",
    "circumscriptively",
    "aerophilous",
    "harmonichord",
    "hodder",
    "morphew",
    "marmot",
    "bechirp",
    "superconformity",
    "undomestic",
    "noncensored",
    "hipe",
    "Teutonize",
    "Uragoga",
    "scribing",
    "endodontic",
    "praetaxation",
    "smirkly",
    "Redemptionist",
    "superreform",
    "haploperistomic",
    "posterioric",
    "waeg",
    "chillsome",
    "Cacajao",
    "unpained",
    "conceptacular",
    "unpracticability",
    "melon",
    "expunction",
    "ticketer",
    "Boulangism",
    "laminiplantar",
    "treatyist",
    "smokish",
    "straighten",
    "hypocrystalline",
    "overlength",
    "definitely",
    "Dalmatian",
    "straightforwards",
    "scoldingly",
    "ulsterette",
    "prevolunteer",
    "redespise",
    "polyhybrid",
    "unhanged",
    "habitable",
    "sare",
    "unscramble",
    "milliform",
    "befume",
    "Panhellenium",
    "malacia",
    "omniparous",
    "relenting",
    "rockallite",
    "Royena",
    "Varangi",
    "cytogenous",
    "gnarl",
    "supercoincidence",
    "plurisyllable",
    "skedaddle",
    "reconstructional",
    "interplical",
    "unclassifiable",
    "infructiferous",
    "foldedly",
    "bronchopulmonary",
    "unlawfully",
    "bridgepot",
    "thinkableness",
    "Sudanian",
    "singability",
    "triflorate",
    "slumwise",
    "Aepyceros",
    "nunch",
    "muskroot",
    "eucrasy",
    "Heraclitic",
    "ungirt",
    "tinkerlike",
    "suppletion",
    "marteline",
    "tympanism",
    "octonal",
    "neebor",
    "Texas",
    "semimembranosus",
    "theodolite",
    "zebu",
    "remediable",
    "unsuccinct",
    "aganglionic",
    "labially",
    "Termitidae",
    "irrevocability",
    "Schopenhauerian",
    "expansile",
    "proproctor",
    "theelol",
    "lumen",
    "Mesua",
    "unslept",
    "greyness",
    "palustrine",
    "eventlessness",
    "danalite",
    "bisymmetric",
    "Opisthocomi",
    "outbent",
    "agronome",
    "schizolaenaceous",
    "retake",
    "subdeb",
    "plotted",
    "palsgravine",
    "Gonystylus",
    "stickfast",
    "pretransmit",
    "muffed",
    "statutory",
    "hinoideous",
    "logistical",
    "centralize",
    "stepchild",
    "foreman",
    "oometry",
    "nubigenous",
    "undiminishable",
    "deutoplasm",
    (const char *)0
  };

  size_t display_scan(CellListScannerPtr &scanner, ostream &out) {
    Key key_comps;
    ByteString bsvalue;
    size_t count = 0;
    while (scanner->get(key_comps, bsvalue)) {
      out << key_comps << "\n";
      count++;
      scanner->forward();
    }
    out << flush;
    return count;
  }

  void check_replaced_files(CellStorePtr &cs, vector<String> &replaced_files_write,
                            ostream &out) {

    const vector<String> &replaced_files_read = cs->get_replaced_files();
    if (replaced_files_read.size() != replaced_files_write.size()) {
      HT_ERRORF("Wrote %lu replaced_files read %lu", (unsigned long)replaced_files_write.size(),
		(unsigned long)replaced_files_read.size());
      exit(EXIT_FAILURE);
    }
    for (size_t ii=0; ii < replaced_files_read.size(); ++ii) {
      if (replaced_files_read[ii] != replaced_files_write[ii]) {
        HT_ERRORF("Wrote %s as %luth replaced file read %s", replaced_files_write[ii].c_str(),
		  (unsigned long)ii,  replaced_files_read[ii].c_str());
        exit(EXIT_FAILURE);
      }
      out << replaced_files_read[ii] << "\n";
    }
  }
  bool gUseIndex = false;
  void maybe_set_use_index(ScanContext *scan_ctx) {
    if (gUseIndex)
      scan_ctx->use_index = true;
  }
}


int main(int argc, char **argv) {
  try {
    struct sockaddr_in addr;
    FsBroker::Lib::ClientPtr client;
    CellStorePtr cs;
    std::ofstream out("CellStoreScanner_test.output");
    size_t wordi=0;
    const char *delete_test = "delete_test";
    char delete_row[256];
    char delete_cf[256];
    const char *select_cf_test = "select_cf_test";
    char select_cf_row[256];
    String cf_foo = "foo";
    vector<String> replaced_files_write;
    TableIdentifier table_id("0");

    // should coalesce and be in 1 block along with trailer
    replaced_files_write.push_back("/hypertable/tables/0/1/default/qyoNKN5rd__dbHKv/cs0");
    replaced_files_write.push_back("/hypertable/tables/0/1/default/qyoNKN5rd__dbHKv/cs100");
    replaced_files_write.push_back("/hypertable/tables/0/1/default/qyoNKN5rd__dbHKv/cs10");
    replaced_files_write.push_back("/hypertable/tables/0/1/default/qyoNKN5rd__dbHKv/cs3");
    replaced_files_write.push_back("/hypertable/tables/0/1/default/qyoNKN5rd__dbHKv/cs1");
    replaced_files_write.push_back("/hypertable/tables/0/1/default/qyoNKN5rd__dbHKv/cs11");

    Config::init(argc, argv);

    if (argc == 2 && strcmp(argv[1], "use_index") == 0)
      gUseIndex = true;

    if (Config::has("help"))
      Usage::dump_and_exit(usage);

    System::initialize(System::locate_install_dir(argv[0]));
    ReactorFactory::initialize(2);

    uint16_t port = Config::properties->get_i16("FsBroker.Port");

    InetAddr::initialize(&addr, "localhost", port);

    ConnectionManagerPtr conn_mgr = make_shared<ConnectionManager>();
    client = std::make_shared<FsBroker::Lib::Client>(conn_mgr, addr, 15000);

    Global::dfs = client;

    if (!client->wait_for_connection(15000)) {
      HT_ERROR("Unable to connect to DFS");
      return 1;
    }

    Global::memory_tracker = new MemoryTracker(0, 0);

    String testdir = "/CellStoreScanner_test";
    client->mkdirs(testdir);

    //String csname = testdir + format("/cs_pid%d", getpid());

    String csname = testdir + "/cs0";
    PropertiesPtr cs_props = make_shared<Properties>();
    AccessGroupOptions::parse_bloom_filter("rows+cols", cs_props);

    SchemaPtr schema ( Schema::new_instance(schema_str) );

    cs = make_shared<CellStoreV7>(Global::dfs.get(), schema);
    HT_TRY("creating cellstore", cs->create(csname.c_str(), 0, cs_props, &table_id));
    cs->set_replaced_files(replaced_files_write);

    DynamicBuffer dbuf(64000);
    char rowbuf[256];
    SerializedKey serkey;
    int64_t timestamp = 1;
    std::vector<SerializedKey> serkeyv;
    std::vector<Key> keyv;
    Key key;
    uint8_t valuebuf[128];
    uint8_t *uptr;
    ByteString bsvalue;
    ScanContextPtr scan_ctx;
    const char *word;
    const char *value = "All work and no play makes jack a dull boy.";

    uptr = valuebuf;
    Serialization::encode_vi32(&uptr, strlen(value));
    strcpy((char *)uptr, value);
    bsvalue.ptr = valuebuf;

    while (dbuf.fill() < 12000) {

      serkey.ptr = dbuf.ptr;
      if ((timestamp % 4) == 0)
        strcpy(rowbuf, "http://www.omega.com/");
      else {
        if (words[wordi] == 0)
          wordi = 0;
        word = words[wordi++];
        sprintf(rowbuf, "http://www.%s.com/", word );
      }

      if (words[wordi] == 0)
        wordi = 0;
      word = words[wordi++];

      create_key_and_append(dbuf, FLAG_INSERT, rowbuf, 1, word, timestamp,
                            timestamp);
      timestamp++;

      serkeyv.push_back(serkey);
    }

    // test delete logic
    {
      // delete row
      serkey.ptr = dbuf.ptr;
      word = delete_test;
      sprintf(delete_row, "http://www.%s.com/delete_row", word );

      create_key_and_append(dbuf, FLAG_INSERT, delete_row, 1, word, timestamp,
                            timestamp);
      timestamp++;
      serkeyv.push_back(serkey);
      serkey.ptr = dbuf.ptr;
      create_key_and_append(dbuf, FLAG_DELETE_ROW, delete_row, 0, "", timestamp,
                            timestamp);
      timestamp++;
      serkeyv.push_back(serkey);
      serkey.ptr = dbuf.ptr;

      // delete column family
      serkey.ptr = dbuf.ptr;
      sprintf(delete_cf, "http://www.%s.com/delete_cf", word );

      create_key_and_append(dbuf, FLAG_INSERT, delete_cf, 1, word, timestamp,
                            timestamp);
      timestamp++;
      serkeyv.push_back(serkey);
      serkey.ptr = dbuf.ptr;
      create_key_and_append(dbuf, FLAG_DELETE_COLUMN_FAMILY, delete_cf, 1, "", timestamp,
                            timestamp);
      timestamp++;
      serkeyv.push_back(serkey);
    }

    // test column family mask logic
    {
      // delete row
      serkey.ptr = dbuf.ptr;
      word = select_cf_test;
      sprintf(select_cf_row, "http://www.%s.com/select_cf_foo", word );

      create_key_and_append(dbuf, FLAG_INSERT, select_cf_row, 2, word, timestamp,
                            timestamp);
      timestamp++;
      serkeyv.push_back(serkey);
    }

    sort(serkeyv.begin(), serkeyv.end());

    keyv.reserve( serkeyv.size() );

    out << "[baseline]\n";
    for (size_t i=0; i<serkeyv.size(); i++) {
      key.load( serkeyv[i] );
      cs->add(key, bsvalue);
      keyv.push_back(key);
      out << key << "\n";
    }

    cs->finalize(&table_id);

    RangeSpec range;
    range.start_row = "";
    range.end_row = Key::END_ROW_MARKER;

    ScanSpecBuilder ssbuilder;
    String column;

    CellListScannerPtr scanner;

    out << "[individual]\n";
    for (size_t i=0; i<keyv.size(); i++) {
      size_t count;
      ssbuilder.clear();
      column = String("tag:") + keyv[i].column_qualifier;
      if (!strcmp(keyv[i].row, select_cf_row))
        column = cf_foo + ":"+ keyv[i].column_qualifier;
      ssbuilder.add_cell(keyv[i].row, column.c_str());
      scan_ctx = make_shared<ScanContext>(TIMESTAMP_MAX, &(ssbuilder.get()), &range,
                                     schema);
      maybe_set_use_index(scan_ctx.get());
      scanner = cs->create_scanner(scan_ctx.get());
      count = display_scan(scanner, out);

      if (strcmp(keyv[i].row, delete_row) && strcmp(keyv[i].row, delete_cf)) {
        HT_ASSERT(count == 1);
      }
    }

    /**
     * issue 976
     */
    out << "[issue976]\n";
    ssbuilder.clear();
    ssbuilder.add_row_interval("http://www.Texas.com/", true,
                               "http://www.Texas.com/", true);
    ssbuilder.add_column("tag:^s");
    scan_ctx = make_shared<ScanContext>(TIMESTAMP_MAX, &(ssbuilder.get()), &range,
                                   schema);
    out << "may_contain(ROW=\"http://www.Texas.com/\", COL=\"tag:^s\") == ";
    out << ((cs->may_contain(scan_ctx.get())) ? "true" : "false") << "\n";

    /**
     * Row operations
     */

    out << "[first-block-row-scan-1]\n";
    ssbuilder.clear();
    ssbuilder.add_row_interval("http://www.Balak.com/", true,
                               "http://www.Boulangism.com/", true);
    scan_ctx = make_shared<ScanContext>(TIMESTAMP_MAX, &(ssbuilder.get()), &range,
                                   schema);
    maybe_set_use_index(scan_ctx.get());
    scanner = cs->create_scanner(scan_ctx.get());
    display_scan(scanner, out);

    out << "[first-block-row-scan-2]\n";
    ssbuilder.clear();
    ssbuilder.add_row_interval("http://www.Balak.com/", false,
                               "http://www.Boulangism.com/", true);
    scan_ctx = make_shared<ScanContext>(TIMESTAMP_MAX, &(ssbuilder.get()), &range,
                                   schema);
    maybe_set_use_index(scan_ctx.get());
    scanner = cs->create_scanner(scan_ctx.get());
    display_scan(scanner, out);

    out << "[first-block-row-scan-3]\n";
    ssbuilder.clear();
    ssbuilder.add_row_interval("http://www.Balak.com/", true,
                               "http://www.Boulangism.com/", false);
    scan_ctx = make_shared<ScanContext>(TIMESTAMP_MAX, &(ssbuilder.get()), &range,
                                   schema);
    maybe_set_use_index(scan_ctx.get());
    scanner = cs->create_scanner(scan_ctx.get());
    display_scan(scanner, out);

    out << "[first-block-row-scan-4]\n";
    ssbuilder.clear();
    ssbuilder.add_row_interval("http://www.Balak.com/", false,
                               "http://www.Boulangism.com/", false);
    scan_ctx = make_shared<ScanContext>(TIMESTAMP_MAX, &(ssbuilder.get()), &range,
                                   schema);
    maybe_set_use_index(scan_ctx.get());
    scanner = cs->create_scanner(scan_ctx.get());
    display_scan(scanner, out);

    out << "[last-block-row-scan-1]\n";
    ssbuilder.clear();
    ssbuilder.add_row_interval("http://www.unlawfully.com/", true,
                               "http://www.unscramble.com/", true);
    scan_ctx = make_shared<ScanContext>(TIMESTAMP_MAX, &(ssbuilder.get()), &range,
                                   schema);
    maybe_set_use_index(scan_ctx.get());
    scanner = cs->create_scanner(scan_ctx.get());
    display_scan(scanner, out);

    out << "[last-block-row-scan-2]\n";
    ssbuilder.clear();
    ssbuilder.add_row_interval("http://www.unlawfully.com/", false,
                               "http://www.unscramble.com/", true);
    scan_ctx = make_shared<ScanContext>(TIMESTAMP_MAX, &(ssbuilder.get()), &range,
                                   schema);
    maybe_set_use_index(scan_ctx.get());
    scanner = cs->create_scanner(scan_ctx.get());
    display_scan(scanner, out);

    out << "[last-block-row-scan-3]\n";
    ssbuilder.clear();
    ssbuilder.add_row_interval("http://www.unlawfully.com/", true,
                               "http://www.unscramble.com/", false);
    scan_ctx = make_shared<ScanContext>(TIMESTAMP_MAX, &(ssbuilder.get()), &range,
                                   schema);
    maybe_set_use_index(scan_ctx.get());
    scanner = cs->create_scanner(scan_ctx.get());
    display_scan(scanner, out);

    out << "[last-block-row-scan-4]\n";
    ssbuilder.clear();
    ssbuilder.add_row_interval("http://www.unlawfully.com/", false,
                               "http://www.unscramble.com/", false);
    scan_ctx = make_shared<ScanContext>(TIMESTAMP_MAX, &(ssbuilder.get()), &range,
                                   schema);
    maybe_set_use_index(scan_ctx.get());
    scanner = cs->create_scanner(scan_ctx.get());
    display_scan(scanner, out);

    out << "[short-row-scan-1]\n";
    ssbuilder.clear();
    ssbuilder.add_row_interval("http://www.Philistia.com/", true,
                               "http://www.Texas.com/", true);
    scan_ctx = make_shared<ScanContext>(TIMESTAMP_MAX, &(ssbuilder.get()), &range,
                                   schema);
    maybe_set_use_index(scan_ctx.get());
    scanner = cs->create_scanner(scan_ctx.get());
    display_scan(scanner, out);

    out << "[short-row-scan-2]\n";
    ssbuilder.clear();
    ssbuilder.add_row_interval("http://www.Philistia.com/", false,
                               "http://www.Texas.com/", true);
    scan_ctx = make_shared<ScanContext>(TIMESTAMP_MAX, &(ssbuilder.get()), &range,
                                   schema);
    maybe_set_use_index(scan_ctx.get());
    scanner = cs->create_scanner(scan_ctx.get());
    display_scan(scanner, out);

    out << "[short-row-scan-3]\n";
    ssbuilder.clear();
    ssbuilder.add_row_interval("http://www.Philistia.com/", true,
                               "http://www.Texas.com/", false);
    scan_ctx = make_shared<ScanContext>(TIMESTAMP_MAX, &(ssbuilder.get()), &range,
                                   schema);
    maybe_set_use_index(scan_ctx.get());
    scanner = cs->create_scanner(scan_ctx.get());
    display_scan(scanner, out);

    out << "[short-row-scan-4]\n";
    ssbuilder.clear();
    ssbuilder.add_row_interval("http://www.Philistia.com/", false,
                               "http://www.Texas.com/", false);
    scan_ctx = make_shared<ScanContext>(TIMESTAMP_MAX, &(ssbuilder.get()), &range,
                                   schema);
    maybe_set_use_index(scan_ctx.get());
    scanner = cs->create_scanner(scan_ctx.get());
    display_scan(scanner, out);

    out << "[block-row-scan-1]\n";
    ssbuilder.clear();
    ssbuilder.add_row_interval("http://www.antiholiday.com/", true,
                               "http://www.carlings.com/", true);
    scan_ctx = make_shared<ScanContext>(TIMESTAMP_MAX, &(ssbuilder.get()), &range,
                                   schema);
    maybe_set_use_index(scan_ctx.get());
    scanner = cs->create_scanner(scan_ctx.get());
    display_scan(scanner, out);

    out << "[block-row-scan-2]\n";
    ssbuilder.clear();
    ssbuilder.add_row_interval("http://www.antiholiday.com/", false,
                               "http://www.carlings.com/", true);
    scan_ctx = make_shared<ScanContext>(TIMESTAMP_MAX, &(ssbuilder.get()), &range,
                                   schema);
    maybe_set_use_index(scan_ctx.get());
    scanner = cs->create_scanner(scan_ctx.get());
    display_scan(scanner, out);

    out << "[block-row-scan-3]\n";
    ssbuilder.clear();
    ssbuilder.add_row_interval("http://www.antiholiday.com/", true,
                               "http://www.carlings.com/", false);
    scan_ctx = make_shared<ScanContext>(TIMESTAMP_MAX, &(ssbuilder.get()), &range,
                                   schema);
    maybe_set_use_index(scan_ctx.get());
    scanner = cs->create_scanner(scan_ctx.get());
    display_scan(scanner, out);

    out << "[block-row-scan-4]\n";
    ssbuilder.clear();
    ssbuilder.add_row_interval("http://www.antiholiday.com/", false,
                               "http://www.carlings.com/", false);
    scan_ctx = make_shared<ScanContext>(TIMESTAMP_MAX, &(ssbuilder.get()), &range,
                                   schema);
    maybe_set_use_index(scan_ctx.get());
    scanner = cs->create_scanner(scan_ctx.get());
    display_scan(scanner, out);

    out << "[big-row-scan-1]\n";
    ssbuilder.clear();
    ssbuilder.add_row_interval("http://www.nonvenous.com/", true,
                               "http://www.omega.com/", true);
    scan_ctx = make_shared<ScanContext>(TIMESTAMP_MAX, &(ssbuilder.get()), &range,
                                   schema);
    maybe_set_use_index(scan_ctx.get());
    scanner = cs->create_scanner(scan_ctx.get());
    display_scan(scanner, out);

    out << "[big-row-scan-2]\n";
    ssbuilder.clear();
    ssbuilder.add_row_interval("http://www.nonvenous.com/", false,
                               "http://www.omega.com/", true);
    scan_ctx = make_shared<ScanContext>(TIMESTAMP_MAX, &(ssbuilder.get()), &range,
                                   schema);
    maybe_set_use_index(scan_ctx.get());
    scanner = cs->create_scanner(scan_ctx.get());
    display_scan(scanner, out);

    out << "[big-row-scan-3]\n";
    ssbuilder.clear();
    ssbuilder.add_row_interval("http://www.nonvenous.com/", true,
                               "http://www.omega.com/", false);
    scan_ctx = make_shared<ScanContext>(TIMESTAMP_MAX, &(ssbuilder.get()), &range,
                                   schema);
    maybe_set_use_index(scan_ctx.get());
    scanner = cs->create_scanner(scan_ctx.get());
    display_scan(scanner, out);

    out << "[big-row-scan-4]\n";
    ssbuilder.clear();
    ssbuilder.add_row_interval("http://www.nonvenous.com/", false,
                               "http://www.omega.com/", false);
    scan_ctx = make_shared<ScanContext>(TIMESTAMP_MAX, &(ssbuilder.get()), &range,
                                   schema);
    maybe_set_use_index(scan_ctx.get());
    scanner = cs->create_scanner(scan_ctx.get());
    display_scan(scanner, out);

    out << "[big-row-scan-5]\n";
    ssbuilder.clear();
    ssbuilder.add_row_interval("http://www.omega.com/", true,
                               "http://www.oometry.com/", true);
    scan_ctx = make_shared<ScanContext>(TIMESTAMP_MAX, &(ssbuilder.get()), &range,
                                   schema);
    maybe_set_use_index(scan_ctx.get());
    scanner = cs->create_scanner(scan_ctx.get());
    display_scan(scanner, out);

    out << "[big-row-scan-6]\n";
    ssbuilder.clear();
    ssbuilder.add_row_interval("http://www.omega.com/", false,
                               "http://www.oometry.com/", true);
    scan_ctx = make_shared<ScanContext>(TIMESTAMP_MAX, &(ssbuilder.get()), &range,
                                   schema);
    maybe_set_use_index(scan_ctx.get());
    scanner = cs->create_scanner(scan_ctx.get());
    display_scan(scanner, out);

    out << "[big-row-scan-7]\n";
    ssbuilder.clear();
    ssbuilder.add_row_interval("http://www.omega.com/", true,
                               "http://www.oometry.com/", false);
    scan_ctx = make_shared<ScanContext>(TIMESTAMP_MAX, &(ssbuilder.get()), &range,
                                   schema);
    maybe_set_use_index(scan_ctx.get());
    scanner = cs->create_scanner(scan_ctx.get());
    display_scan(scanner, out);

    out << "[big-row-scan-8]\n";
    ssbuilder.clear();
    ssbuilder.add_row_interval("http://www.omega.com/", false,
                               "http://www.oometry.com/", false);
    scan_ctx = make_shared<ScanContext>(TIMESTAMP_MAX, &(ssbuilder.get()), &range,
                                   schema);
    maybe_set_use_index(scan_ctx.get());
    scanner = cs->create_scanner(scan_ctx.get());
    display_scan(scanner, out);

    out << "[last-short-row-scan-1]\n";
    ssbuilder.clear();
    ssbuilder.add_row_interval("http://www.urolithology.com/", true,
                               "http://www.vipresident.com/", true);
    scan_ctx = make_shared<ScanContext>(TIMESTAMP_MAX, &(ssbuilder.get()), &range,
                                   schema);
    maybe_set_use_index(scan_ctx.get());
    scanner = cs->create_scanner(scan_ctx.get());
    display_scan(scanner, out);

    out << "[last-short-row-scan-2]\n";
    ssbuilder.clear();
    ssbuilder.add_row_interval("http://www.urolithology.com/", false,
                               "http://www.vipresident.com/", true);
    scan_ctx = make_shared<ScanContext>(TIMESTAMP_MAX, &(ssbuilder.get()), &range,
                                   schema);
    maybe_set_use_index(scan_ctx.get());
    scanner = cs->create_scanner(scan_ctx.get());
    display_scan(scanner, out);

    out << "[last-short-row-scan-3]\n";
    ssbuilder.clear();
    ssbuilder.add_row_interval("http://www.urolithology.com/", true,
                               "http://www.vipresident.com/", false);
    scan_ctx = make_shared<ScanContext>(TIMESTAMP_MAX, &(ssbuilder.get()), &range,
                                   schema);
    maybe_set_use_index(scan_ctx.get());
    scanner = cs->create_scanner(scan_ctx.get());
    display_scan(scanner, out);

    out << "[last-short-row-scan-4]\n";
    ssbuilder.clear();
    ssbuilder.add_row_interval("http://www.urolithology.com/", false,
                               "http://www.vipresident.com/", false);
    scan_ctx = make_shared<ScanContext>(TIMESTAMP_MAX, &(ssbuilder.get()), &range,
                                   schema);
    maybe_set_use_index(scan_ctx.get());
    scanner = cs->create_scanner(scan_ctx.get());
    display_scan(scanner, out);

    out << "[last-block-row-scan-1]\n";
    ssbuilder.clear();
    ssbuilder.add_row_interval("http://www.utick.com/", true,
                               "http://www.younglet.com/", true);
    scan_ctx = make_shared<ScanContext>(TIMESTAMP_MAX, &(ssbuilder.get()), &range,
                                   schema);
    maybe_set_use_index(scan_ctx.get());
    scanner = cs->create_scanner(scan_ctx.get());
    display_scan(scanner, out);

    out << "[last-block-row-scan-2]\n";
    ssbuilder.clear();
    ssbuilder.add_row_interval("http://www.utick.com/", false,
                               "http://www.younglet.com/", true);
    scan_ctx = make_shared<ScanContext>(TIMESTAMP_MAX, &(ssbuilder.get()), &range,
                                   schema);
    maybe_set_use_index(scan_ctx.get());
    scanner = cs->create_scanner(scan_ctx.get());
    display_scan(scanner, out);

    out << "[last-block-row-scan-3]\n";
    ssbuilder.clear();
    ssbuilder.add_row_interval("http://www.utick.com/", true,
                               "http://www.younglet.com/", false);
    scan_ctx = make_shared<ScanContext>(TIMESTAMP_MAX, &(ssbuilder.get()), &range,
                                   schema);
    maybe_set_use_index(scan_ctx.get());
    scanner = cs->create_scanner(scan_ctx.get());
    display_scan(scanner, out);

    out << "[last-block-row-scan-4]\n";
    ssbuilder.clear();
    ssbuilder.add_row_interval("http://www.utick.com/", false,
                               "http://www.younglet.com/", false);
    scan_ctx = make_shared<ScanContext>(TIMESTAMP_MAX, &(ssbuilder.get()), &range,
                                   schema);
    maybe_set_use_index(scan_ctx.get());
    scanner = cs->create_scanner(scan_ctx.get());
    display_scan(scanner, out);

    /**
     * Test deletes
     */

    out << "[delete-row-cell-scan]\n";
    ssbuilder.clear();
    String deleted_row = (String) delete_row;
    String deleted_column = (String)"tag:" + delete_test;
    ssbuilder.add_cell(deleted_row.c_str(), deleted_column.c_str());
    scan_ctx = make_shared<ScanContext>(TIMESTAMP_MAX, &(ssbuilder.get()), &range,
                                   schema);
    maybe_set_use_index(scan_ctx.get());
    scanner = cs->create_scanner(scan_ctx.get());
    display_scan(scanner, out);

    out << "[delete-cf-cell-scan]\n";
    ssbuilder.clear();
    deleted_row = (String) delete_cf;
    ssbuilder.add_cell(deleted_row.c_str(), deleted_column.c_str());
    scan_ctx = make_shared<ScanContext>(TIMESTAMP_MAX, &(ssbuilder.get()), &range,
                                   schema);
    maybe_set_use_index(scan_ctx.get());
    scanner = cs->create_scanner(scan_ctx.get());
    display_scan(scanner, out);

    /**
     * Test column family scan
     */
    out << "[select-column-family-scan]\n";
    ssbuilder.clear();
    ssbuilder.add_column(cf_foo.c_str());
    scan_ctx = make_shared<ScanContext>(TIMESTAMP_MAX, &(ssbuilder.get()), &range,
                                   schema);
    maybe_set_use_index(scan_ctx.get());
    scanner = cs->create_scanner(scan_ctx.get());
    display_scan(scanner, out);

    out << "[select-column-family-row-scan]\n";
    ssbuilder.clear();
    String cf_foo_row = (String) select_cf_row;
    ssbuilder.add_cell(cf_foo_row.c_str(), cf_foo.c_str());
    scan_ctx = make_shared<ScanContext>(TIMESTAMP_MAX, &(ssbuilder.get()), &range,
                                   schema);
    maybe_set_use_index(scan_ctx.get());
    scanner = cs->create_scanner(scan_ctx.get());
    display_scan(scanner, out);

    /**
     * Cell operations
     */

    out << "[first-block-cell-scan-1]\n";
    ssbuilder.clear();
    ssbuilder.add_cell_interval("http://www.Balak.com/", "tag:micasize", true,
        "http://www.Boulangism.com/", "tag:laminiplantar", true);
    scan_ctx = make_shared<ScanContext>(TIMESTAMP_MAX, &(ssbuilder.get()), &range,
                                   schema);
    maybe_set_use_index(scan_ctx.get());
    scanner = cs->create_scanner(scan_ctx.get());
    display_scan(scanner, out);

    out << "[first-block-cell-scan-2]\n";
    ssbuilder.clear();
    ssbuilder.add_cell_interval("http://www.Balak.com/", "tag:micasize", false,
        "http://www.Boulangism.com/", "tag:laminiplantar", true);
    scan_ctx = make_shared<ScanContext>(TIMESTAMP_MAX, &(ssbuilder.get()), &range,
                                   schema);
    maybe_set_use_index(scan_ctx.get());
    scanner = cs->create_scanner(scan_ctx.get());
    display_scan(scanner, out);

    out << "[first-block-cell-scan-3]\n";
    ssbuilder.clear();
    ssbuilder.add_cell_interval("http://www.Balak.com/", "tag:micasize", true,
        "http://www.Boulangism.com/", "tag:laminiplantar", false);
    scan_ctx = make_shared<ScanContext>(TIMESTAMP_MAX, &(ssbuilder.get()), &range,
                                   schema);
    maybe_set_use_index(scan_ctx.get());
    scanner = cs->create_scanner(scan_ctx.get());
    display_scan(scanner, out);

    out << "[first-block-cell-scan-4]\n";
    ssbuilder.clear();
    ssbuilder.add_cell_interval("http://www.Balak.com/", "tag:micasize", false,
        "http://www.Boulangism.com/", "tag:laminiplantar", false);
    scan_ctx = make_shared<ScanContext>(TIMESTAMP_MAX, &(ssbuilder.get()), &range,
                                   schema);
    maybe_set_use_index(scan_ctx.get());
    scanner = cs->create_scanner(scan_ctx.get());
    display_scan(scanner, out);

    out << "[last-block-cell-scan-1]\n";
    ssbuilder.clear();
    ssbuilder.add_cell_interval("http://www.unlawfully.com/", "tag:bridgepot",
        true, "http://www.unscramble.com/", "tag:milliform", true);
    scan_ctx = make_shared<ScanContext>(TIMESTAMP_MAX, &(ssbuilder.get()), &range,
                                   schema);
    maybe_set_use_index(scan_ctx.get());
    scanner = cs->create_scanner(scan_ctx.get());
    display_scan(scanner, out);

    out << "[last-block-cell-scan-2]\n";
    ssbuilder.clear();
    ssbuilder.add_cell_interval("http://www.unlawfully.com/", "tag:bridgepot",
        false, "http://www.unscramble.com/", "tag:milliform", true);
    scan_ctx = make_shared<ScanContext>(TIMESTAMP_MAX, &(ssbuilder.get()), &range,
                                   schema);
    maybe_set_use_index(scan_ctx.get());
    scanner = cs->create_scanner(scan_ctx.get());
    display_scan(scanner, out);

    out << "[last-block-cell-scan-3]\n";
    ssbuilder.clear();
    ssbuilder.add_cell_interval("http://www.unlawfully.com/", "tag:bridgepot",
        true, "http://www.unscramble.com/", "tag:milliform", false);
    scan_ctx = make_shared<ScanContext>(TIMESTAMP_MAX, &(ssbuilder.get()), &range,
                                   schema);
    maybe_set_use_index(scan_ctx.get());
    scanner = cs->create_scanner(scan_ctx.get());
    display_scan(scanner, out);

    out << "[last-block-cell-scan-4]\n";
    ssbuilder.clear();
    ssbuilder.add_cell_interval("http://www.unlawfully.com/", "tag:bridgepot",
        false, "http://www.unscramble.com/", "tag:milliform", false);
    scan_ctx = make_shared<ScanContext>(TIMESTAMP_MAX, &(ssbuilder.get()), &range,
                                   schema);
    maybe_set_use_index(scan_ctx.get());
    scanner = cs->create_scanner(scan_ctx.get());
    display_scan(scanner, out);

    out << "[short-cell-scan-1]\n";
    ssbuilder.clear();
    ssbuilder.add_cell_interval("http://www.Philistia.com/", "tag:tropic", true,
        "http://www.Texas.com/", "tag:semimembranosus", true);
    scan_ctx = make_shared<ScanContext>(TIMESTAMP_MAX, &(ssbuilder.get()), &range,
                                   schema);
    maybe_set_use_index(scan_ctx.get());
    scanner = cs->create_scanner(scan_ctx.get());
    display_scan(scanner, out);

    out << "[short-cell-scan-2]\n";
    ssbuilder.clear();
    ssbuilder.add_cell_interval("http://www.Philistia.com/", "tag:tropic",
        false, "http://www.Texas.com/", "tag:semimembranosus", true);
    scan_ctx = make_shared<ScanContext>(TIMESTAMP_MAX, &(ssbuilder.get()), &range,
                                   schema);
    maybe_set_use_index(scan_ctx.get());
    scanner = cs->create_scanner(scan_ctx.get());
    display_scan(scanner, out);

    out << "[short-cell-scan-3]\n";
    ssbuilder.clear();
    ssbuilder.add_cell_interval("http://www.Philistia.com/", "tag:tropic", true,
        "http://www.Texas.com/", "tag:semimembranosus", false);
    scan_ctx = make_shared<ScanContext>(TIMESTAMP_MAX, &(ssbuilder.get()), &range,
                                   schema);
    maybe_set_use_index(scan_ctx.get());
    scanner = cs->create_scanner(scan_ctx.get());
    display_scan(scanner, out);

    out << "[short-cell-scan-4]\n";
    ssbuilder.clear();
    ssbuilder.add_cell_interval("http://www.Philistia.com/", "tag:tropic",
        false, "http://www.Texas.com/", "tag:semimembranosus", false);
    scan_ctx = make_shared<ScanContext>(TIMESTAMP_MAX, &(ssbuilder.get()), &range,
                                   schema);
    maybe_set_use_index(scan_ctx.get());
    scanner = cs->create_scanner(scan_ctx.get());
    display_scan(scanner, out);

    out << "[block-cell-scan-1]\n";
    ssbuilder.clear();
    ssbuilder.add_cell_interval("http://www.antiholiday.com/", "tag:benzolize",
        true, "http://www.carlings.com/", "tag:dilogy", true);
    scan_ctx = make_shared<ScanContext>(TIMESTAMP_MAX, &(ssbuilder.get()), &range,
                                   schema);
    maybe_set_use_index(scan_ctx.get());
    scanner = cs->create_scanner(scan_ctx.get());
    display_scan(scanner, out);

    out << "[block-cell-scan-2]\n";
    ssbuilder.clear();
    ssbuilder.add_cell_interval("http://www.antiholiday.com/", "tag:benzolize",
        false, "http://www.carlings.com/", "tag:dilogy", true);
    scan_ctx = make_shared<ScanContext>(TIMESTAMP_MAX, &(ssbuilder.get()), &range,
                                   schema);
    maybe_set_use_index(scan_ctx.get());
    scanner = cs->create_scanner(scan_ctx.get());
    display_scan(scanner, out);

    out << "[block-cell-scan-3]\n";
    ssbuilder.clear();
    ssbuilder.add_cell_interval("http://www.antiholiday.com/", "tag:benzolize",
        true, "http://www.carlings.com/", "tag:dilogy", false);
    scan_ctx = make_shared<ScanContext>(TIMESTAMP_MAX, &(ssbuilder.get()), &range,
                                   schema);
    maybe_set_use_index(scan_ctx.get());
    scanner = cs->create_scanner(scan_ctx.get());
    display_scan(scanner, out);

    out << "[block-cell-scan-4]\n";
    ssbuilder.clear();
    ssbuilder.add_cell_interval("http://www.antiholiday.com/", "tag:benzolize",
        false, "http://www.carlings.com/", "tag:dilogy", false);
    scan_ctx = make_shared<ScanContext>(TIMESTAMP_MAX, &(ssbuilder.get()), &range,
                                   schema);
    maybe_set_use_index(scan_ctx.get());
    scanner = cs->create_scanner(scan_ctx.get());
    display_scan(scanner, out);

    out << "[big-cell-scan-1]\n";
    ssbuilder.clear();
    ssbuilder.add_cell_interval("http://www.nonvenous.com/", "tag:overbloom",
        true, "http://www.omega.com/", "tag:muskroot", true);
    scan_ctx = make_shared<ScanContext>(TIMESTAMP_MAX, &(ssbuilder.get()), &range,
                                   schema);
    maybe_set_use_index(scan_ctx.get());
    scanner = cs->create_scanner(scan_ctx.get());
    display_scan(scanner, out);

    out << "[big-cell-scan-2]\n";
    ssbuilder.clear();
    ssbuilder.add_cell_interval("http://www.nonvenous.com/", "tag:overbloom",
        false, "http://www.omega.com/", "tag:muskroot", true);
    scan_ctx = make_shared<ScanContext>(TIMESTAMP_MAX, &(ssbuilder.get()), &range,
                                   schema);
    maybe_set_use_index(scan_ctx.get());
    scanner = cs->create_scanner(scan_ctx.get());
    display_scan(scanner, out);

    out << "[big-cell-scan-3]\n";
    ssbuilder.clear();
    ssbuilder.add_cell_interval("http://www.nonvenous.com/", "tag:overbloom",
        true, "http://www.omega.com/", "tag:muskroot", false);
    scan_ctx = make_shared<ScanContext>(TIMESTAMP_MAX, &(ssbuilder.get()), &range,
                                   schema);
    maybe_set_use_index(scan_ctx.get());
    scanner = cs->create_scanner(scan_ctx.get());
    display_scan(scanner, out);

    out << "[big-cell-scan-4]\n";
    ssbuilder.clear();
    ssbuilder.add_cell_interval("http://www.nonvenous.com/", "tag:overbloom",
        false, "http://www.omega.com/", "tag:muskroot", false);
    scan_ctx = make_shared<ScanContext>(TIMESTAMP_MAX, &(ssbuilder.get()), &range,
                                   schema);
    maybe_set_use_index(scan_ctx.get());
    scanner = cs->create_scanner(scan_ctx.get());
    display_scan(scanner, out);

    out << "[big-cell-scan-5]\n";
    ssbuilder.clear();
    ssbuilder.add_cell_interval("http://www.omega.com/", "tag:muskroot", true,
        "http://www.oometry.com/", "tag:nubigenous", true);
    scan_ctx = make_shared<ScanContext>(TIMESTAMP_MAX, &(ssbuilder.get()), &range,
                                   schema);
    maybe_set_use_index(scan_ctx.get());
    scanner = cs->create_scanner(scan_ctx.get());
    display_scan(scanner, out);

    out << "[big-cell-scan-6]\n";
    ssbuilder.clear();
    ssbuilder.add_cell_interval("http://www.omega.com/", "tag:muskroot", false,
        "http://www.oometry.com/", "tag:nubigenous", true);
    scan_ctx = make_shared<ScanContext>(TIMESTAMP_MAX, &(ssbuilder.get()), &range,
                                   schema);
    maybe_set_use_index(scan_ctx.get());
    scanner = cs->create_scanner(scan_ctx.get());
    display_scan(scanner, out);

    out << "[big-cell-scan-7]\n";
    ssbuilder.clear();
    ssbuilder.add_cell_interval("http://www.omega.com/", "tag:muskroot", true,
        "http://www.oometry.com/", "tag:nubigenous", false);
    scan_ctx = make_shared<ScanContext>(TIMESTAMP_MAX, &(ssbuilder.get()), &range,
                                   schema);
    maybe_set_use_index(scan_ctx.get());
    scanner = cs->create_scanner(scan_ctx.get());
    display_scan(scanner, out);

    out << "[big-cell-scan-8]\n";
    ssbuilder.clear();
    ssbuilder.add_cell_interval("http://www.omega.com/", "tag:muskroot", false,
        "http://www.oometry.com/", "tag:nubigenous", false);
    scan_ctx = make_shared<ScanContext>(TIMESTAMP_MAX, &(ssbuilder.get()), &range,
                                   schema);
    maybe_set_use_index(scan_ctx.get());
    scanner = cs->create_scanner(scan_ctx.get());
    display_scan(scanner, out);

    out << "[last-short-cell-scan-1]\n";
    ssbuilder.clear();
    ssbuilder.add_cell_interval("http://www.urolithology.com/",
        "tag:presynaptic", true, "http://www.vipresident.com/", "tag:coiling",
        true);
    scan_ctx = make_shared<ScanContext>(TIMESTAMP_MAX, &(ssbuilder.get()), &range,
                                   schema);
    maybe_set_use_index(scan_ctx.get());
    scanner = cs->create_scanner(scan_ctx.get());
    display_scan(scanner, out);

    out << "[last-short-cell-scan-2]\n";
    ssbuilder.clear();
    ssbuilder.add_cell_interval("http://www.urolithology.com/",
        "tag:presynaptic", false, "http://www.vipresident.com/", "tag:coiling",
        true);
    scan_ctx = make_shared<ScanContext>(TIMESTAMP_MAX, &(ssbuilder.get()), &range,
                                   schema);
    maybe_set_use_index(scan_ctx.get());
    scanner = cs->create_scanner(scan_ctx.get());
    display_scan(scanner, out);

    out << "[last-short-cell-scan-3]\n";
    ssbuilder.clear();
    ssbuilder.add_cell_interval("http://www.urolithology.com/",
        "tag:presynaptic", true, "http://www.vipresident.com/", "tag:coiling",
        false);
    scan_ctx = make_shared<ScanContext>(TIMESTAMP_MAX, &(ssbuilder.get()), &range,
                                   schema);
    maybe_set_use_index(scan_ctx.get());
    scanner = cs->create_scanner(scan_ctx.get());
    display_scan(scanner, out);

    out << "[last-short-cell-scan-4]\n";
    ssbuilder.clear();
    ssbuilder.add_cell_interval("http://www.urolithology.com/",
        "tag:presynaptic", false, "http://www.vipresident.com/", "tag:coiling",
        false);
    scan_ctx = make_shared<ScanContext>(TIMESTAMP_MAX, &(ssbuilder.get()), &range,
                                   schema);
    maybe_set_use_index(scan_ctx.get());
    scanner = cs->create_scanner(scan_ctx.get());
    display_scan(scanner, out);

    out << "[last-block-cell-scan-1]\n";
    ssbuilder.clear();
    ssbuilder.add_cell_interval("http://www.utick.com/", "tag:frike", true,
        "http://www.younglet.com/", "tag:laeotropism", true);
    scan_ctx = make_shared<ScanContext>(TIMESTAMP_MAX, &(ssbuilder.get()), &range,
                               schema);
    maybe_set_use_index(scan_ctx.get());
    scanner = cs->create_scanner(scan_ctx.get());
    display_scan(scanner, out);

    out << "[last-block-cell-scan-2]\n";
    ssbuilder.clear();
    ssbuilder.add_cell_interval("http://www.utick.com/", "tag:frike", false,
        "http://www.younglet.com/", "tag:laeotropism", true);
    scan_ctx = make_shared<ScanContext>(TIMESTAMP_MAX, &(ssbuilder.get()), &range,
                               schema);
    maybe_set_use_index(scan_ctx.get());
    scanner = cs->create_scanner(scan_ctx.get());
    display_scan(scanner, out);

    out << "[last-block-cell-scan-3]\n";
    ssbuilder.clear();
    ssbuilder.add_cell_interval("http://www.utick.com/", "tag:frike", true,
        "http://www.younglet.com/", "tag:laeotropism", false);
    scan_ctx = make_shared<ScanContext>(TIMESTAMP_MAX, &(ssbuilder.get()), &range,
                               schema);
    maybe_set_use_index(scan_ctx.get());
    scanner = cs->create_scanner(scan_ctx.get());
    display_scan(scanner, out);

    out << "[last-block-cell-scan-4]\n";
    ssbuilder.clear();
    ssbuilder.add_cell_interval("http://www.utick.com/", "tag:frike", false,
        "http://www.younglet.com/", "tag:laeotropism", false);
    scan_ctx = make_shared<ScanContext>(TIMESTAMP_MAX, &(ssbuilder.get()), &range,
                               schema);
    maybe_set_use_index(scan_ctx.get());
    scanner = cs->create_scanner(scan_ctx.get());
    display_scan(scanner, out);

    out << "[replaced-files-0]\n";
    cs = CellStoreFactory::open(csname, "", "http://www.omega.com/");
    check_replaced_files(cs, replaced_files_write, out);

    out << "[cs-range-0]\n";

    cs = CellStoreFactory::open(csname, "", "http://www.omega.com/");

    ssbuilder.clear();
    ssbuilder.add_row_interval("", true, Key::END_ROW_MARKER, true);
    scan_ctx = make_shared<ScanContext>(TIMESTAMP_MAX, &(ssbuilder.get()), &range,
                               schema);
    maybe_set_use_index(scan_ctx.get());
    scanner = cs->create_scanner(scan_ctx.get());
    display_scan(scanner, out);

    out << "[cs-range-1]\n";
    cs = CellStoreFactory::open(csname, "http://www.omega.com/", Key::END_ROW_MARKER);

    ssbuilder.clear();
    ssbuilder.add_row_interval("", true, Key::END_ROW_MARKER, true);
    scan_ctx = make_shared<ScanContext>(TIMESTAMP_MAX, &(ssbuilder.get()), &range,
                               schema);
    maybe_set_use_index(scan_ctx.get());
    scanner = cs->create_scanner(scan_ctx.get());
    display_scan(scanner, out);

    csname = testdir + "/cs1";
    cs_props = make_shared<Properties>();
    cs_props->set("blocksize", (int32_t)10000);
    cs_props->set("compressor", String("none"));
    cs = make_shared<CellStoreV7>(Global::dfs.get(), schema);
    HT_TRY("creating cellstore", cs->create(csname.c_str(), 0, cs_props, &table_id));
    // should not coalesce and be in a separate block from trailer
    replaced_files_write.push_back("1/hypertable/tables/0/1/default/qyoNKN5rd__dbHKv/cs0");
    replaced_files_write.push_back("2/hypertable/tables/0/1/default/qyoNKN5rd__dbHKv/cs0");
    replaced_files_write.push_back("3/hypertable/tables/0/1/default/qyoNKN5rd__dbHKv/cs0");
    replaced_files_write.push_back("4/hypertable/tables/0/1/default/qyoNKN5rd__dbHKv/cs0");
    replaced_files_write.push_back("5/hypertable/tables/0/1/default/qyoNKN5rd__dbHKv/cs0");
    replaced_files_write.push_back("6/hypertable/tables/0/1/default/qyoNKN5rd__dbHKv/cs0");
    cs->set_replaced_files(replaced_files_write);

    value = "Like a lot of new ideas, Media Cloud started with a long-running argument among friends.  Ethan Zuckerman and a handful of";
    uptr = valuebuf;
    Serialization::encode_vi32(&uptr, strlen(value));
    strcpy((char *)uptr, value);
    bsvalue.ptr = valuebuf;

    memset(&key, 0, sizeof(key));

    for (size_t i=0; i<500; i++) {
      sprintf(rowbuf, "row%06d", (int)i);
      dbuf.clear();
      serkey.ptr = dbuf.ptr;
      create_key_and_append(dbuf, FLAG_INSERT, rowbuf, 1, "");
      key.load(serkey);
      cs->add(key, bsvalue);
    }
    cs->finalize(&table_id);

    /**
       BLOCK INDEX:
       0: offset=0 size=10100 row=row000072
       1: offset=10100 size=10100 row=row000145
       2: offset=20200 size=10100 row=row000218
       3: offset=30300 size=10100 row=row000291
       4: offset=40400 size=10100 row=row000364
       5: offset=50500 size=10100 row=row000437
       6: offset=60600 size=8582 row=row000499
     **/
    out << "[replaced-files-1]\n";
    cs = CellStoreFactory::open(csname, "", "row000200/");
    check_replaced_files(cs, replaced_files_write, out);

    out << "[range-restriction-1]\n";
    cs = CellStoreFactory::open(csname, "", "row000200");


    ssbuilder.clear();
    ssbuilder.add_row_interval("row000050", true,
                               "row000450", true);
    scan_ctx = make_shared<ScanContext>(TIMESTAMP_MAX, &(ssbuilder.get()), &range, schema);
    maybe_set_use_index(scan_ctx.get());
    scanner = cs->create_scanner(scan_ctx.get());
    display_scan(scanner, out);

    out << "[range-restriction-2]\n";
    cs = CellStoreFactory::open(csname, "", "row000218");
    ssbuilder.clear();
    ssbuilder.add_row_interval("row000071", true,
                               "row000365", true);
    scan_ctx = make_shared<ScanContext>(TIMESTAMP_MAX, &(ssbuilder.get()), &range, schema);
    maybe_set_use_index(scan_ctx.get());
    scanner = cs->create_scanner(scan_ctx.get());
    display_scan(scanner, out);

    out << "[range-restriction-3]\n";
    cs = CellStoreFactory::open(csname, "row000400", Key::END_ROW_MARKER);
    ssbuilder.clear();
    ssbuilder.add_row_interval("row000300", true,
                               Key::END_ROW_MARKER, true);
    scan_ctx = make_shared<ScanContext>(TIMESTAMP_MAX, &(ssbuilder.get()), &range, schema);
    maybe_set_use_index(scan_ctx.get());
    scanner = cs->create_scanner(scan_ctx.get());
    display_scan(scanner, out);


    out << "[range-restriction-4]\n";
    cs = CellStoreFactory::open(csname, "row000400", Key::END_ROW_MARKER);
    ssbuilder.clear();
    ssbuilder.add_row_interval("row000364", true,
                               Key::END_ROW_MARKER, true);
    scan_ctx = make_shared<ScanContext>(TIMESTAMP_MAX, &(ssbuilder.get()), &range, schema);
    maybe_set_use_index(scan_ctx.get());
    scanner = cs->create_scanner(scan_ctx.get());
    display_scan(scanner, out);


    out << "[range-restriction-5]\n";
    cs = CellStoreFactory::open(csname, "row000218", "row000291");
    ssbuilder.clear();
    ssbuilder.add_row_interval("", true,
                               Key::END_ROW_MARKER, true);
    scan_ctx = make_shared<ScanContext>(TIMESTAMP_MAX, &(ssbuilder.get()), &range, schema);
    maybe_set_use_index(scan_ctx.get());
    scanner = cs->create_scanner(scan_ctx.get());
    display_scan(scanner, out);

    out << "[range-restriction-6]\n";
    cs = CellStoreFactory::open(csname, "row000218", "row000291");
    ssbuilder.clear();
    ssbuilder.add_row_interval("row000250", true,
                               "row000275", true);
    scan_ctx = make_shared<ScanContext>(TIMESTAMP_MAX, &(ssbuilder.get()), &range, schema);
    maybe_set_use_index(scan_ctx.get());
    scanner = cs->create_scanner(scan_ctx.get());
    display_scan(scanner, out);

    /**
     * test trailer
     */
    csname = testdir + "/cs2";
    cs_props = make_shared<Properties>();

    schema.reset( Schema::new_instance(schema2_str) );

    cs = make_shared<CellStoreV7>(Global::dfs.get(), schema);
    HT_TRY("creating cellstore", cs->create(csname.c_str(), 0, cs_props, &table_id));
    // should coalesce and be in 2 blocks, with the 2nd block also containing the trailer
    replaced_files_write.push_back("7/hypertable/tables/0/1/default/qyoNKN5rd__dbHKv/cs0");
    replaced_files_write.push_back("8/hypertable/tables/0/1/default/qyoNKN5rd__dbHKv/cs0");
    replaced_files_write.push_back("9/hypertable/tables/0/1/default/qyoNKN5rd__dbHKv/cs0");
    cs->set_replaced_files(replaced_files_write);

    wordi = 0;
    serkeyv.clear();
    while (dbuf.fill() < 12000) {
      serkey.ptr = dbuf.ptr;
      if (words[wordi] == 0)
        wordi = 0;
      word = words[wordi++];
      sprintf(rowbuf, "http://www.%s.com/", word );

      if (words[wordi] == 0)
        wordi = 0;
      word = words[wordi++];
      create_key_and_append(dbuf, FLAG_INSERT, rowbuf, (wordi%3)+1, word, timestamp,
                            timestamp);
      timestamp++;
      serkeyv.push_back(serkey);
    }

    for (size_t i=0; i<serkeyv.size(); i++) {
      key.load( serkeyv[i] );
      cs->add(key, bsvalue);
      keyv.push_back(key);
    }

    cs->finalize(&table_id);
    cs = CellStoreFactory::open(csname, "", "row000200/");
    out << "[replaced-files-2]\n";
    check_replaced_files(cs, replaced_files_write, out);

    int64_t expiration_time = boost::any_cast<int64_t>(cs->get_trailer()->get("expiration_time"));
    int64_t expirable_data = boost::any_cast<int64_t>(cs->get_trailer()->get("expirable_data"));
    int64_t delete_count = boost::any_cast<int64_t>(cs->get_trailer()->get("delete_count"));
    out << "trailer.expiration_time = " << expiration_time << "\n";
    out << "trailer.expirable_data = " << expirable_data << "\n";
    out << "trailer.delete_count= " << delete_count << "\n";

    // close scanner
    scanner = 0;

    // issue 1017
    out << "[issue1017]\n";
    csname = testdir + "/cs3";
    cs_props = make_shared<Properties>();
    AccessGroupOptions::parse_bloom_filter("rows", cs_props);
    schema.reset( Schema::new_instance(schema_str) );

    cs = make_shared<CellStoreV7>(Global::dfs.get(), schema);
    HT_TRY("creating cellstore", cs->create(csname.c_str(), 735, cs_props, &table_id));
    strcpy((char *)rowbuf, "the only row");
    value = "Dummy value";
    uptr = valuebuf;
    Serialization::encode_vi32(&uptr, strlen(value));
    strcpy((char *)uptr, value);
    bsvalue.ptr = valuebuf;

    for (size_t i=0; i<1000; i++) {
      dbuf.clear();
      serkey.ptr = dbuf.ptr;
      create_key_and_append(dbuf, FLAG_INSERT, rowbuf, 1, "");
      key.load(serkey);
      cs->add(key, bsvalue);
    }
    cs->finalize(&table_id);

    ssbuilder.clear();
    ssbuilder.add_row_interval("the only row", true, "the only row", true);
    scan_ctx = make_shared<ScanContext>(TIMESTAMP_MAX, &(ssbuilder.get()),&range,schema);
    out << "may_contain(ROW=\"the only row\") == ";
    out << ((cs->may_contain(scan_ctx.get())) ? "true" : "false") << "\n";

    ssbuilder.clear();
    ssbuilder.add_row_interval("absent row", true, "absent row", true);
    scan_ctx = make_shared<ScanContext>(TIMESTAMP_MAX, &(ssbuilder.get()),&range,schema);
    out << "may_contain(ROW=\"absent row\") == ";
    out << ((cs->may_contain(scan_ctx.get())) ? "true" : "false") << "\n";

    cs = 0;

    out << flush;

    String cmd_str = "diff CellStoreScanner_test.output "
                     "CellStoreScanner_test.golden";
    if (system(cmd_str.c_str()) != 0)
      return 1;

    client->rmdir(testdir);
  }
  catch (Exception &e) {
    HT_ERROR_OUT << e << HT_END;
    return 1;
  }
  catch (...) {
    HT_ERROR_OUT << "unexpected exception caught" << HT_END;
    return 1;
  }
  return 0;
}

