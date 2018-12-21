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

#include <Hypertable/Lib/Client.h>
#include <Hypertable/Lib/KeySpec.h>

#include <AsyncComm/ReactorFactory.h>

#include <Common/StringExt.h>
#include <Common/Init.h>
#include <Common/Error.h>
#include <Common/InetAddr.h>
#include <Common/Logger.h>
#include <Common/ServerLauncher.h>
#include <Common/System.h>
#include <Common/Usage.h>

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <vector>
#include <algorithm>
#include <fstream>

extern "C" {
#include <unistd.h>
}

using namespace Hypertable;
using namespace Hypertable::Config;
using namespace std;

namespace {
  const char *usage[] = {
    "usage: MutatorNoLogSync <path to install binary dir>",
    "",
    "This program tests the RangeServer no commit log sync feature.",
    "It launches a 3 RangeServers and writes some data to them via a mutator ",
    "in no log sync mode. The first write is about 30 cells, after which rangeservers ",
    "are restarted. This is repeated 2 more times with writes of 4 cells each, followed ",
    "by a full table scan. This test is designed to verify the no log sync mode works ",
    "correctly in the case of buffers being flushed automatically, explicitly and on ",
    "mutator destruction especially when buffers are flushed to multiple rangeservers.",
    0
  };

  void start_rangeservers(vector<ServerLauncher *> &rangeservers, uint32_t n,
                          String split_size_arg="", uint32_t sleep_sec=2);
  void stop_rangeservers(vector<ServerLauncher *> &rangeservers, uint32_t sleep_sec=2);
  void restart_rangeservers(vector<ServerLauncher *> &rangeservers, uint32_t sleep_sec=2);

  const char *numbers[] = {
    "0123456789",
    "2345678901",
    "4567890123",
    "6789012345",
    "8901234567",
    "1234567890",
    "3456789012",
    "5678901234",
    "7890123456",
    "9012345678",
    "2143658709",
    "4365870921",
    "6587092143",
    "8709214365",
    "0921436587",
    "1032547698",
    "3254769810",
    "5476981032",
    "7698103254",
    "9810325476",
    "3216549870",
    "4327650981",
    "5438761092",
    "1236548790",
    "2347658901",
    "0123012344",
    "2345234566",
    "4567456788",
    "6789678900",
    "8901890122",
    "1234123455",
    "3456345677",
    "5678567899",
    "7890789011",
    "9012901233",
    "2143214366",
    "4365436588",
    "6587658700",
    "8709870922",
    "0921092144",
    "1032103255",
    "3254325477",
    "5476547699",
    "7698769811",
    "9810981033",
    "3216321655",
    "4327432766",
    "5438543877",
    "1236123655",
    "2347234766",
    (const char *)0
  };

  const char *words[] = {
    "prolifiprolificcprolifiprolifiprolifiprolificcprolifiprolificccc",
    "yonsideyonsidexxyonsideyonsideyonsideyonsidexxyonsideyonsidexxxx",
    "testametestamenntestametestametestametestamenntestametestamennnn",
    "hypostahypostasshypostahypostahypostahypostasshypostahypostassss",
    "scaglioscagliollscaglioscaglioscaglioscagliollscaglioscagliollll",
    "bandymabandymannbandymabandymabandymabandymannbandymabandymannnn",
    "proteinproteiniiproteinproteinproteinproteiniiproteinproteiniiii",
    "paleothpaleotheepaleothpaleothpaleothpaleotheepaleothpaleotheeee",
    "heteronheteroneeheteronheteronheteronheteroneeheteronheteroneeee",
    "undeligundelighhundeligundeligundeligundelighhundeligundelighhhh",
    "MegachiMegachillMegachiMegachiMegachiMegachillMegachiMegachillll",
    "heparinhepariniiheparinheparinheparinhepariniiheparinhepariniiii",
    "salamansalamanddsalamansalamansalamansalamanddsalamansalamandddd",
    "stereosstereosppstereosstereosstereosstereosppstereosstereospppp",
    "favorabfavorabllfavorabfavorabfavorabfavorabllfavorabfavorabllll",
    "aerolitaerolitiiaerolitaerolitaerolitaerolitiiaerolitaerolitiiii",
    "ovopyriovopyriffovopyriovopyriovopyriovopyriffovopyriovopyriffff",
    "persecupersecuttpersecupersecupersecupersecuttpersecupersecutttt",
    "fondlesfondlesoofondlesfondlesfondlesfondlesoofondlesfondlesoooo",
    "DiplodoDiplodoccDiplodoDiplodoDiplodoDiplodoccDiplodoDiplodocccc",
    "federalfederaliifederalfederalfederalfederaliifederalfederaliiii",
    "folkfrefolkfreeefolkfrefolkfrefolkfrefolkfreeefolkfrefolkfreeeee",
    "doorlikdoorlikeedoorlikdoorlikdoorlikdoorlikeedoorlikdoorlikeeee",
    "healinghealingllhealinghealinghealinghealingllhealinghealingllll",
    "rugositrugosityyrugositrugositrugositrugosityyrugositrugosityyyy",
    "underhounderhorrunderhounderhounderhounderhorrunderhounderhorrrr",
    "depancredepancredepancredepancredepancredepancredepancredepancre",
    "gnathothgnathothgnathothgnathothgnathothgnathothgnathothgnathoth",
    "micasizemicasizemicasizemicasizemicasizemicasizemicasizemicasize",
    "garnetwogarnetwogarnetwogarnetwogarnetwogarnetwogarnetwogarnetwo",
    "humoralihumoralihumoralihumoralihumoralihumoralihumoralihumorali",
    "PhilistiPhilistiPhilistiPhilistiPhilistiPhilistiPhilistiPhilisti",
    "cloysomecloysomecloysomecloysomecloysomecloysomecloysomecloysome",
    "oxypropioxypropioxypropioxypropioxypropioxypropioxypropioxypropi",
    "upholstrupholstrupholstrupholstrupholstrupholstrupholstrupholstr",
    "therolattherolattherolattherolattherolattherolattherolattherolat",
    "seaworthseaworthseaworthseaworthseaworthseaworthseaworthseaworth",
    "nigrificnigrificnigrificnigrificnigrificnigrificnigrificnigrific",
    "CalystegCalystegCalystegCalystegCalystegCalystegCalystegCalysteg",
    "bulimifobulimifobulimifobulimifobulimifobulimifobulimifobulimifo",
    "leptotenleptotenleptotenleptotenleptotenleptotenleptotenleptoten",
    "schreineschreineschreineschreineschreineschreineschreineschreine",
    "laemodiplaemodiplaemodiplaemodiplaemodiplaemodiplaemodiplaemodip",
    "MelastomMelastomMelastomMelastomMelastomMelastomMelastomMelastom",
    "nonaluminonaluminonaluminonaluminonaluminonaluminonaluminonalumi",
    "nonsensenonsensenonsensenonsensenonsensenonsensenonsensenonsense",
    "chatoyanchatoyanchatoyanchatoyanchatoyanchatoyanchatoyanchatoyan",
    "PomacentPomacentPomacentPomacentPomacentPomacentPomacentPomacent",
    "implorinimplorinimplorinimplorinimplorinimplorinimplorinimplorin",
    "syngenessyngenessyngenessyngenessyngenessyngenessyngenessyngenes",
    "infidellinfidellinfidellinfidellinfidellinfidellinfidellinfidell",
    "architecarchitecarchitecarchitecarchitecarchitecarchitecarchitec",
    "unwifeliunwifeliunwifeliunwifeliunwifeliunwifeliunwifeliunwifeli",
    "fringentfringentfringentfringentfringentfringentfringentfringent",
    "planetleplanetleplanetleplanetleplanetleplanetleplanetleplanetle",
    "rehandlerehandlerehandlerehandlerehandlerehandlerehandlerehandle",
    "lucidneslucidneslucidneslucidneslucidneslucidneslucidneslucidnes",
    "anthracyanthracyanthracyanthracyanthracyanthracyanthracyanthracy",
    "PalaeophPalaeophPalaeophPalaeophPalaeophPalaeophPalaeophPalaeoph",
    "thinglikthinglikthinglikthinglikthinglikthinglikthinglikthinglik",
    "worsermeworsermeworsermeworsermeworsermeworsermeworsermeworserme",
    "phallitiphallitiphallitiphallitiphallitiphallitiphallitiphalliti",
    "blaenessblaenessblaenessblaenessblaenessblaenessblaenessblaeness",
    "AchernarAchernarAchernarAchernarAchernarAchernarAchernarAchernar",
    "AcroceraAcroceraAcroceraAcroceraAcroceraAcroceraAcroceraAcrocera",
    "foreseeaforeseeaforeseeaforeseeaforeseeaforeseeaforeseeaforeseea",
    "phytooecphytooecphytooecphytooecphytooecphytooecphytooecphytooec",
    "palaeostpalaeostpalaeostpalaeostpalaeostpalaeostpalaeostpalaeost",
    "enfuddleenfuddleenfuddleenfuddleenfuddleenfuddleenfuddleenfuddle",
    "LaemodipLaemodipLaemodipLaemodipLaemodipLaemodipLaemodipLaemodip",
    "urolithourolithourolithourolithourolithourolithourolithourolitho",
    "presynappresynappresynappresynappresynappresynappresynappresynap",
    "harmonogharmonogharmonogharmonogharmonogharmonogharmonogharmonog",
    "preadequpreadequpreadequpreadequpreadequpreadequpreadequpreadequ",
    "bucketfubucketfubucketfubucketfubucketfubucketfubucketfubucketfu",
    "redoubtaredoubtaredoubtaredoubtaredoubtaredoubtaredoubtaredoubta",
    "submaximsubmaximsubmaximsubmaximsubmaximsubmaximsubmaximsubmaxim",
    "sociogensociogensociogensociogensociogensociogensociogensociogen",
    "epididymepididymepididymepididymepididymepididymepididymepididym",
    "capsulitcapsulitcapsulitcapsulitcapsulitcapsulitcapsulitcapsulit",
    "intercomintercomintercomintercomintercomintercomintercomintercom",
    "understaunderstaunderstaunderstaunderstaunderstaunderstaundersta",
    "chrysomochrysomochrysomochrysomochrysomochrysomochrysomochrysomo",
    "couserancouserancouserancouserancouserancouserancouserancouseran",
    "untranspuntranspuntranspuntranspuntranspuntranspuntranspuntransp",
    "argillifargillifargillifargillifargillifargillifargillifargillif",
    "AlkoraniAlkoraniAlkoraniAlkoraniAlkoraniAlkoraniAlkoraniAlkorani",
    "antisynoantisynoantisynoantisynoantisynoantisynoantisynoantisyno",
    (const char *)0
    };

  const char *schema =
  "<Schema group_commit_interval=\"100\">"
  "  <AccessGroup name=\"default\">"
  "    <ColumnFamily>"
  "      <Name>Field</Name>"
  "    </ColumnFamily>"
  "  </AccessGroup>"
  "</Schema>";

  String install_dir;
}


int main(int argc, char **argv) {
  ClientPtr hypertable_client_ptr;
  NamespacePtr namespace_ptr;
  TablePtr table_ptr;
  KeySpec key;
  String start_all, clean_db;
  String config_file = "/MutatorNoLogSyncTest.cfg";
  String value;
  ScanSpecBuilder scan_spec;
  Cell cell;
  String ht_bin_path;
  vector<ServerLauncher *> rangeservers;
  uint32_t num_rs = 3;
  uint32_t value_size=50;
  int ii;
  uint32_t key_size = 10;
  String row_key;
  key.row_len = key_size;
  vector<String> expected_output;

  Config::init(0, 0);

  if ((argc > 1 && (!strcmp(argv[1], "-?") || !strcmp(argv[1], "--help"))) || argc <=1)
    Usage::dump_and_exit(usage);

  ht_bin_path = argv[1];

  install_dir = System::install_dir;

  // Setup dirs /links
  unlink("rs1.out");unlink("rs2.out");unlink("rs3.out");
  unlink("./htRangeServer");
  HT_ASSERT(link("../RangeServer/htRangeServer",
                 "./htRangeServer") == 0);
  unlink("./ht_serverup");
  HT_ASSERT(link("../../Tools/serverup/ht_serverup", "./ht_serverup") == 0);
  system("mkdir -p conf");
  system("touch conf/METADATA.xml");
  system("touch conf/RS_METRICS.xml");

  config_file = install_dir + config_file;
  Config::parse_file(config_file, file_desc());
  start_all = ht_bin_path + "/ht-start-test-servers.sh --clear --no-rangeserver"
              " --no-thriftbroker --config=" + config_file;
  if (system(start_all.c_str()) !=0) {
    HT_ERROR("Unable to start servers");
    exit(EXIT_FAILURE);
  }
  sleep(2);

  // launch rangeservers with small split size so we have multiple ranges on multiple servers
  start_rangeservers(rangeservers, num_rs, "--Hypertable.RangeServer.Range.SplitSize=3000");

  ReactorFactory::initialize(2);
  try {
    assert (config_file != "");
    hypertable_client_ptr =
      make_shared<Hypertable::Client>(System::locate_install_dir(argv[0]),
                                      config_file);
    namespace_ptr = hypertable_client_ptr->open_namespace("/");
    namespace_ptr->drop_table("MutatorNoLogSyncTest", true);
    namespace_ptr->create_table("MutatorNoLogSyncTest", schema);
    table_ptr = namespace_ptr->open_table("MutatorNoLogSyncTest");
  }
  catch (Hypertable::Exception &e) {
    cerr << e << endl;
    stop_rangeservers(rangeservers);
    HT_EXPECT(system(clean_db.c_str()) == 0, Error::EXTERNAL);
    quick_exit(EXIT_FAILURE);
  }

  key.column_family = "Field";

  try {
    // Load up some data so we have at least 3 ranges and do an explicit flush
    {
      TableMutatorPtr mutator_ptr(table_ptr->create_mutator(0, Table::MUTATOR_FLAG_NO_LOG_SYNC));
      cout << "*** Load 1" << endl;
      for (ii=0; ii<30; ++ii) {
        row_key = numbers[ii];
        key.row = row_key.c_str();
        value = words[ii];
        mutator_ptr->set(key, value.c_str(), value_size);
        sleep(1);
        expected_output.push_back(row_key + " " + value.substr(0,value_size));
      }
    }

    // sleep so ranges can split
    sleep(1);

    // restart rangeservers
    restart_rangeservers(rangeservers);

    // Load up a bit more and do an explicit flush
    {
      TableMutatorPtr mutator_ptr(table_ptr->create_mutator(0, Table::MUTATOR_FLAG_NO_LOG_SYNC));
       cout << "*** Load 2" << endl;
      for (;ii<34; ++ii) {
        row_key = numbers[ii];
        key.row = row_key.c_str();
        value = words[ii];
        mutator_ptr->set(key, value.c_str(), value_size);
        expected_output.push_back(row_key + " " + value.substr(0,value_size));
      }
      mutator_ptr->flush();
    }

    // sleep so ranges can split
    sleep(1);

    // restart rangeservers
    restart_rangeservers(rangeservers);

    // Test flush on mutator destruction
    {
      TableMutatorPtr mutator_ptr(table_ptr->create_mutator(0, Table::MUTATOR_FLAG_NO_LOG_SYNC));
      cout << "*** Load 3" << endl;
      for (;ii<38; ++ii) {
        row_key = numbers[ii];
        key.row = row_key.c_str();
        value = words[ii];
        mutator_ptr->set(key, value.c_str(), value_size);
        expected_output.push_back(row_key + " " + value.substr(0,value_size));
      }
    }

    // restart rangeservers
    restart_rangeservers(rangeservers);
    // sleep for a while
    sleep(30);

    // Do a scan and verify results
    {
      String scan_result;
      TableScannerPtr scanner_ptr(table_ptr->create_scanner(scan_spec.get()));

      int num_cells = expected_output.size();
      sort(expected_output.begin(), expected_output.end());
      ii = 0;
      scan_spec.clear();
      cout << "*** Full table scan:" << endl;
      while (scanner_ptr->next(cell)) {
        String value((const char*) cell.value, cell.value_len);
        scan_result = (String)cell.row_key + " " + value;

        if (ii >= num_cells ) {
          cout << "Expected " << num_cells << " cells from scan, got back " << ii << endl;
          HT_THROW(Error::FAILED_EXPECTATION, "Unexpected scan result");
        }

        if (expected_output[ii] != scan_result) {
          cout << "Expected cell["<< ii <<"] = '" << expected_output[ii] << "' ";
          cout << "Received cell["<< ii <<"] = '" << scan_result << "'" << endl;
          HT_THROW(Error::FAILED_EXPECTATION, "Unexpected scan result");
        }

        ++ii;
        cout << cell.row_key << " "<<  value << endl;
      }

      if (ii != num_cells) {
        cout << "Expected " << num_cells << " cells from scan, got back " << ii << endl;
        HT_THROW(Error::FAILED_EXPECTATION, "Unexpected scan result");
      }
    }

  }
  catch (Hypertable::Exception &e) {
    cerr << e << endl;
    cout << "Test failed" << endl;
    stop_rangeservers(rangeservers);
    HT_EXPECT(system(clean_db.c_str()) == 0, Error::EXTERNAL);
    quick_exit(EXIT_FAILURE);
  }

  // stop all servers
  stop_rangeservers(rangeservers);
  HT_EXPECT(system(clean_db.c_str()) == 0, Error::EXTERNAL);

  cout << "Test passed"<< endl;

  exit(EXIT_SUCCESS);
}


namespace {
  void check_rangeserver(uint32_t port) {
    //make syscall to serverup and make sure RangeServer is up
    String command;
    command = (String)"./ht_serverup --Hypertable.RangeServer.ReadyStatus=WARNING --wait 5000 --silent --range-server localhost:"
      + port + (String)" rangeserver";
    if (system(command.c_str()) !=0) {
      HT_ERRORF("RangeServer on port %d did not come up", port);
      exit(EXIT_FAILURE);
    }
  }

  void start_rangeservers(vector<ServerLauncher *> &rangeservers, uint32_t nn,
                          String split_size_arg, uint32_t sleep_sec)
  {
    ServerLauncher *rs;
    uint32_t base_port = 15870;
    vector<const char *> rs_args;
    String port;
    String outfile;
    static int file_counter=1;

    for (uint32_t ii=0; ii<nn; ii++) {
      String proxy_name = (String)"--Hypertable.RangeServer.ProxyName=rs" + (ii+1);
      outfile = (String)"rs" + (file_counter++) + (String)".out";
      port = (String) "--Hypertable.RangeServer.Port=" + (base_port+ii);
      rs_args.clear();
      rs_args.push_back("Hypertable.RangeServer");
      rs_args.push_back(port.c_str());
      rs_args.push_back("--config=./MutatorNoLogSyncTest.cfg");
      rs_args.push_back(proxy_name.c_str());

      if (split_size_arg != "")
        rs_args.push_back(split_size_arg.c_str());

      rs_args.push_back("--debug");
      rs_args.push_back((const char *)0);

      rs = new ServerLauncher("./htRangeServer", (char * const *)&rs_args[0],
                              outfile.c_str());
      rangeservers.push_back(rs);
    }
    sleep(sleep_sec);
    // make sure the RangeServers started ow throw exception
    for (uint32_t ii=0; ii<nn; ii++) {
      check_rangeserver(base_port + ii);
    }

  }


  void stop_rangeservers(vector<ServerLauncher *> &rangeservers, uint32_t sleep_sec)
  {
    ServerLauncher *rs;

    for (vector<ServerLauncher *>::iterator iter = rangeservers.begin();
         iter != rangeservers.end(); ++iter) {
      rs = *iter;
      delete rs;
    }
    rangeservers.clear();
    sleep(sleep_sec);
  }

  void restart_rangeservers(vector<ServerLauncher *> &rangeservers, uint32_t sleep_sec)
  {
    uint32_t nn = rangeservers.size();
    stop_rangeservers(rangeservers);
    start_rangeservers(rangeservers, nn, "", sleep_sec);
  }
}
