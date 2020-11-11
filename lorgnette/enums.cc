// Copyright 2020 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "lorgnette/enums.h"

#include <base/logging.h>

DocumentScanSaneBackend SaneBackendFromString(const std::string& name) {
  if (name == "abaton")
    return kAbaton;
  if (name == "agfafocus")
    return kAgfafocus;
  if (name == "airscan") {
    // TODO(fletcherw): expand this to specify the manufacturer of an airscan
    // scanner.
    return kAirscanOther;
  }
  if (name == "apple")
    return kApple;
  if (name == "artec")
    return kArtec;
  if (name == "artec_eplus48u")
    return kArtecEplus48U;
  if (name == "as6e")
    return kAs6E;
  if (name == "avision")
    return kAvision;
  if (name == "bh")
    return kBh;
  if (name == "canon")
    return kCanon;
  if (name == "canon630u")
    return kCanon630U;
  if (name == "canon_dr")
    return kCanonDr;
  if (name == "cardscan")
    return kCardscan;
  if (name == "coolscan")
    return kCoolscan;
  if (name == "coolscan2")
    return kCoolscan2;
  if (name == "coolscan3")
    return kCoolscan3;
  if (name == "dc210")
    return kDc210;
  if (name == "dc240")
    return kDc240;
  if (name == "dc25")
    return kDc25;
  if (name == "dell1600n_net")
    return kDell1600NNet;
  if (name == "dmc")
    return kDmc;
  if (name == "epjitsu")
    return kEpjitsu;
  if (name == "epson")
    return kEpson;
  if (name == "epson2")
    return kEpson2;
  if (name == "escl")
    return kEscl;
  if (name == "fujitsu")
    return kFujitsu;
  if (name == "genesys")
    return kGenesys;
  if (name == "gt68xx")
    return kGt68Xx;
  if (name == "hp")
    return kHp;
  if (name == "hp3500")
    return kHp3500;
  if (name == "hp3900")
    return kHp3900;
  if (name == "hp4200")
    return kHp4200;
  if (name == "hp5400")
    return kHp5400;
  if (name == "hp5590")
    return kHp5590;
  if (name == "hpljm1005")
    return kHpljm1005;
  if (name == "hs2p")
    return kHs2P;
  if (name == "ibm")
    return kIbm;
  if (name == "ippusb") {
    // TODO(b/160472550): expand this to specify the manufacturer of an airscan
    // scanner.
    return kIppUsbOther;
  }
  if (name == "kodak")
    return kKodak;
  if (name == "kodakaio")
    return kKodakaio;
  if (name == "kvs1025")
    return kKvs1025;
  if (name == "kvs20xx")
    return kKvs20Xx;
  if (name == "kvs40xx")
    return kKvs40Xx;
  if (name == "leo")
    return kLeo;
  if (name == "lexmark")
    return kLexmark;
  if (name == "ma1509")
    return kMa1509;
  if (name == "magicolor")
    return kMagicolor;
  if (name == "matsushita")
    return kMatsushita;
  if (name == "microtek")
    return kMicrotek;
  if (name == "microtek2")
    return kMicrotek2;
  if (name == "mustek")
    return kMustek;
  if (name == "mustek_usb")
    return kMustekUsb;
  if (name == "mustek_usb2")
    return kMustekUsb2;
  if (name == "nec")
    return kNec;
  if (name == "net")
    return kNet;
  if (name == "niash")
    return kNiash;
  if (name == "p5")
    return kP5;
  if (name == "pie")
    return kPie;
  if (name == "pixma")
    return kPixma;
  if (name == "plustek")
    return kPlustek;
  if (name == "plustek_pp")
    return kPlustekPp;
  if (name == "qcam")
    return kQcam;
  if (name == "ricoh")
    return kRicoh;
  if (name == "ricoh2")
    return kRicoh2;
  if (name == "rts8891")
    return kRts8891;
  if (name == "s9036")
    return kS9036;
  if (name == "sceptre")
    return kSceptre;
  if (name == "sharp")
    return kSharp;
  if (name == "sm3600")
    return kSm3600;
  if (name == "sm3840")
    return kSm3840;
  if (name == "snapscan")
    return kSnapscan;
  if (name == "sp15c")
    return kSp15C;
  if (name == "st400")
    return kSt400;
  if (name == "stv680")
    return kStv680;
  if (name == "tamarack")
    return kTamarack;
  if (name == "teco1")
    return kTeco1;
  if (name == "teco2")
    return kTeco2;
  if (name == "teco3")
    return kTeco3;
  if (name == "test")
    return kTest;
  if (name == "u12")
    return kU12;
  if (name == "umax")
    return kUmax;
  if (name == "umax1220u")
    return kUmax1220U;
  if (name == "umax_pp")
    return kUmaxPp;
  if (name == "xerox_mfp")
    return kXeroxMfp;
  LOG(WARNING) << "Unknown sane backend " << name;
  return kOtherBackend;
}
