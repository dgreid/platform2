#!/usr/bin/python2
# -*- coding: utf-8 -*-
# Copyright 2015 The Chromium OS Authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

"""Authoritative source for Chromium OS region/locale configuration.

Run this module to display all known regions (use --help to see options).
"""

from __future__ import print_function

import argparse
import collections
import json
import re
import sys

import yaml


# The regular expression to check values in Region.keyboards and Region.locales.
# Keyboards should come with xkb: protocol, or the input methods (ime:, m17n:).
# Examples: xkb:us:intl:eng, ime:ime:zh-t:cangjie
KEYBOARD_PATTERN = re.compile(r'^xkb:\w+:\w*:\w+$|'
                              r'^(ime|m17n|t13n):[\w:-]+$')
# Locale should be a combination of language and location.
# Examples: en-US, ja.
LOCALE_PATTERN = re.compile(r'^(\w+)(-[A-Z0-9]+)?$')


class Enum(frozenset):
  """An enumeration type.

  Usage:
    To create a enum object:
      dummy_enum = Enum(['A', 'B', 'C'])

    To access a enum object, use:
      dummy_enum.A
      dummy_enum.B
  """

  def __getattr__(self, name):
    if name in self:
      return name
    raise AttributeError


class RegionException(Exception):
  """Exception in Region handling."""
  pass


def MakeList(value):
  """Converts the given value to a list.

  Returns:
    A list of elements from "value" if it is iterable (except string);
    otherwise, a list contains only one element.
  """
  if (isinstance(value, collections.Iterable) and
      not isinstance(value, basestring)):
    return list(value)
  return [value]


class Region(object):
  """Comprehensive, standard locale configuration per country/region.

  See :ref:`regions-values` for detailed information on how to set these values.
  """
  # pylint gets confused by some of the docstrings.
  # pylint: disable=C0322

  # ANSI = US-like
  # ISO = UK-like
  # JIS = Japanese
  # KS = Korean
  # ABNT2 = Brazilian (like ISO but with an extra key to the left of the
  #   right shift key)
  KeyboardMechanicalLayout = Enum(['ANSI', 'ISO', 'JIS', 'KS', 'ABNT2'])

  region_code = None
  """A unique identifier for the region.  This may be a lower-case
  `ISO 3166-1 alpha-2 code
  <http://en.wikipedia.org/wiki/ISO_3166-1_alpha-2>`_ (e.g., ``us``),
  a variant within an alpha-2 entity (e.g., ``ca.fr``), or an
  identifier for a collection of countries or entities (e.g.,
  ``latam-es-419`` or ``nordic``).  See :ref:`region-codes`.

  Note that ``uk`` is not a valid identifier; ``gb`` is used as it is
  the real alpha-2 code for the UK."""

  keyboards = None
  """A list of keyboard layout identifiers (e.g., ``xkb:us:intl:eng``
  or ``m17n:ar``). This field was designed to be the physical keyboard layout
  in the beginning, and then becomes a list of OOBE keyboard selection, which
  then includes non-physical layout elements like input methods (``ime:``).
  To avoid confusion, physical layout is now defined by
  :py:attr:`keyboard_mechanical_layout`, and this is reserved for logical
  layouts.

  This is identical to the legacy VPD ``keyboard_layout`` value."""

  time_zones = None
  """A list of default `tz database time zone
  <http://en.wikipedia.org/wiki/List_of_tz_database_time_zones>`_
  identifiers (e.g., ``America/Los_Angeles``). See
  `timezone_settings.cc <http://goo.gl/WSVUeE>`_ for supported time
  zones.

  This is identical to the legacy VPD ``initial_timezone`` value."""

  locales = None
  """A list of default locale codes (e.g., ``en-US``); see
  `l10n_util.cc <http://goo.gl/kVkht>`_ for supported locales.

  This is identital to the legacy VPD ``initial_locale`` field."""

  keyboard_mechanical_layout = None
  """The keyboard's mechanical layout (``ANSI`` [US-like], ``ISO``
  [UK-like], ``JIS`` [Japanese], ``ABNT2`` [Brazilian] or ``KS`` [Korean])."""

  description = None
  """A human-readable description of the region.
  This defaults to :py:attr:`region_code` if not set."""

  notes = None
  """Implementation notes about the region.  This may be None."""

  numeric_id = None
  """An integer for mapping into Chrome OS HWID.
  Please never change this once it is assigned."""

  regulatory_domain = None
  """An ISO 3166-1 alpha 2 upper-cased two-letter region code for setting
  Wireless regulatory. See crosbug.com/p/38745 for more details.

  When omitted, this will derive from region_code."""

  confirmed = None
  """An optional boolean flag to indicate if the region data is confirmed."""

  FIELDS = ['numeric_id', 'region_code', 'description', 'keyboards',
            'time_zones', 'locales', 'keyboard_mechanical_layout',
            'regulatory_domain']
  """Names of fields that define the region."""

  def __init__(self, region_code, keyboards, time_zones, locales,
               keyboard_mechanical_layout, description=None, notes=None,
               numeric_id=None, regdomain=None):
    """Constructor.

    Args:
      region_code: See :py:attr:`region_code`.
      keyboards: See :py:attr:`keyboards`.  A single string is accepted for
        backward compatibility.
      time_zones: See :py:attr:`time_zones`.
      locales: See :py:attr:`locales`.  A single string is accepted
        for backward compatibility.
      keyboard_mechanical_layout: See :py:attr:`keyboard_mechanical_layout`.
      description: See :py:attr:`description`.
      notes: See :py:attr:`notes`.
      numeric_id: See :py:attr:`numeric_id`.  This must be None or a
        non-negative integer.
      regdomain: See :py:attr:`regulatory_domain`.
    """

    def regdomain_from_region(region):
      if region.find('.') >= 0:
        region = region[:region.index('.')]
      if len(region) == 2:
        return region.upper()
      return None

    # Quick check: should be 'gb', not 'uk'
    if region_code == 'uk':
      raise RegionException("'uk' is not a valid region code (use 'gb')")

    self.region_code = region_code
    self.keyboards = MakeList(keyboards)
    self.time_zones = MakeList(time_zones)
    self.locales = MakeList(locales)
    self.keyboard_mechanical_layout = keyboard_mechanical_layout
    self.description = description or region_code
    self.notes = notes
    self.numeric_id = numeric_id
    self.regulatory_domain = (regdomain or regdomain_from_region(region_code))
    self.confirmed = None

    if self.numeric_id is not None:
      if not isinstance(self.numeric_id, int):
        raise TypeError('Numeric ID is %r but should be an integer' %
                        (self.numeric_id,))
      if self.numeric_id < 0:
        raise ValueError('Numeric ID is %r but should be non-negative' %
                         self.numeric_id)

    for f in (self.keyboards, self.locales):
      assert all(isinstance(x, str) for x in f), (
          'Expected a list of strings, not %r' % f)
    for f in self.keyboards:
      assert KEYBOARD_PATTERN.match(f), (
          'Keyboard pattern %r does not match %r' % (
              f, KEYBOARD_PATTERN.pattern))
    for f in self.locales:
      assert LOCALE_PATTERN.match(f), (
          'Locale %r does not match %r' % (
              f, LOCALE_PATTERN.pattern))
    assert (self.regulatory_domain and
            len(self.regulatory_domain) == 2 and
            self.regulatory_domain.upper() == self.regulatory_domain), (
                'Regulatory domain settings error for region %s' % region_code)

  def __repr__(self):
    return 'Region(%s)' % (', '.join([getattr(self, x) for x in self.FIELDS]))

  def GetFieldsDict(self):
    """Returns a dict of all substantive fields.

    notes and description are excluded.
    """
    return dict((k, getattr(self, k)) for k in self.FIELDS)

_KML = Region.KeyboardMechanicalLayout
REGIONS_LIST = [
    Region(
        'au', 'xkb:us::eng', 'Australia/Sydney', 'en-AU', _KML.ANSI,
        'Australia', None, 1),
    Region(
        'be', 'xkb:be::nld', 'Europe/Brussels', 'en-GB', _KML.ISO,
        'Belgium',
        'Flemish (Belgian Dutch) keyboard; British English language for '
        'neutrality', 2),
    Region(
        'br', 'xkb:br::por', 'America/Sao_Paulo', 'pt-BR', _KML.ABNT2,
        'Brazil (ABNT2)',
        (
            'ABNT2 = ABNT NBR 10346 variant 2. This is the preferred layout '
            'for Brazil. ABNT2 is mostly an ISO layout, but it 12 keys between '
            'the shift keys; see http://goo.gl/twA5tq'), 3),
    Region(
        'br.abnt', 'xkb:br::por', 'America/Sao_Paulo', 'pt-BR',
        _KML.ISO, 'Brazil (ABNT)',
        (
            'Like ABNT2, but lacking the extra key to the left of the right '
            'shift key found in that layout. ABNT2 (the "br" region) is '
            'preferred to this layout'), 4),
    Region(
        'br.usintl', 'xkb:us:intl:eng', 'America/Sao_Paulo', 'pt-BR',
        _KML.ANSI, 'Brazil (US Intl)',
        'Brazil with US International keyboard layout. ABNT2 ("br") and '
        'ABNT1 ("br.abnt1 ") are both preferred to this.', 5),
    Region(
        'ca.ansi', 'xkb:us::eng', 'America/Toronto', 'en-CA', _KML.ANSI,
        'Canada (US keyboard)',
        'Canada with US (ANSI) keyboard. Not for en/fr hybrid ANSI '
        'keyboards; for that you would want ca.hybridansi. See '
        'http://goto/cros-canada', 6),
    Region(
        'ca.fr', 'xkb:ca::fra', 'America/Toronto', 'fr-CA', _KML.ISO,
        'Canada (French keyboard)',
        (
            'Canadian French (ISO) keyboard. The most common configuration for '
            'Canadian French SKUs.  See http://goto/cros-canada'), 7),
    Region(
        'ca.hybrid', 'xkb:ca:eng:eng', 'America/Toronto', 'en-CA',
        _KML.ISO, 'Canada (hybrid ISO)',
        (
            'Canada with hybrid (ISO) xkb:ca:eng:eng + xkb:ca::fra keyboard, '
            'defaulting to English language and keyboard.  Used only if there '
            'needs to be a single SKU for all of Canada.  See '
            'http://goto/cros-canada'), 8),
    Region(
        'ca.hybridansi', 'xkb:ca:eng:eng', 'America/Toronto', 'en-CA',
        _KML.ANSI, 'Canada (hybrid ANSI)',
        (
            'Canada with hybrid (ANSI) xkb:ca:eng:eng + xkb:ca::fra keyboard, '
            'defaulting to English language and keyboard.  Used only if there '
            'needs to be a single SKU for all of Canada.  See '
            'http://goto/cros-canada'), 9),
    Region(
        'ca.multix', 'xkb:ca:multix:fra', 'America/Toronto', 'fr-CA',
        _KML.ISO, 'Canada (multilingual)',
        (
            "Canadian Multilingual keyboard; you probably don't want this. See "
            'http://goto/cros-canada'), 10),
    Region(
        'ch', 'xkb:ch::ger', 'Europe/Zurich', 'en-US', _KML.ISO,
        'Switzerland',
        'German keyboard, but US English to be language-neutral; used in '
        'the common case that there is only a single Swiss SKU.', 11),
    Region(
        'de', 'xkb:de::ger', 'Europe/Berlin', 'de', _KML.ISO, 'Germany',
        None, 12),
    Region(
        'es', 'xkb:es::spa', 'Europe/Madrid', 'es', _KML.ISO, 'Spain',
        None, 13),
    Region(
        'fi', 'xkb:fi::fin', 'Europe/Helsinki', 'fi', _KML.ISO, 'Finland',
        None, 14),
    Region(
        'fr', 'xkb:fr::fra', 'Europe/Paris', 'fr', _KML.ISO, 'France',
        None, 15),
    Region(
        'gb', 'xkb:gb:extd:eng', 'Europe/London', 'en-GB', _KML.ISO, 'UK',
        None, 16),
    Region(
        'ie', 'xkb:gb:extd:eng', 'Europe/Dublin', 'en-GB', _KML.ISO,
        'Ireland', None, 17),
    Region(
        'in', 'xkb:us::eng', 'Asia/Calcutta', 'en-US', _KML.ANSI, 'India',
        None, 18),
    Region(
        'it', 'xkb:it::ita', 'Europe/Rome', 'it', _KML.ISO, 'Italy', None,
        19),
    Region(
        'latam-es-419', 'xkb:es::spa', 'America/Mexico_City', 'es-419',
        _KML.ISO, 'Hispanophone Latin America',
        (
            'Spanish-speaking countries in Latin America, using the Iberian '
            '(Spain) Spanish keyboard, which is increasingly dominant in '
            'Latin America. Known to be correct for '
            'Chile, Colombia, Mexico, Peru; '
            'still unconfirmed for other es-419 countries. The old Latin '
            'American layout (xkb:latam::spa) has not been approved; before '
            'using that you must seek review through http://goto/vpdsettings. '
            'See also http://goo.gl/Iffuqh. Note that 419 is the UN M.49 '
            'region code for Latin America'), 20, 'MX'),
    Region(
        'my', 'xkb:us::eng', 'Asia/Kuala_Lumpur', 'ms', _KML.ANSI,
        'Malaysia', None, 21),
    Region(
        'nl', 'xkb:us:intl:eng', 'Europe/Amsterdam', 'nl', _KML.ANSI,
        'Netherlands', None, 22),
    Region(
        'nordic', 'xkb:se::swe', 'Europe/Stockholm', 'en-US', _KML.ISO,
        'Nordics',
        (
            'Unified SKU for Sweden, Norway, and Denmark.  This defaults '
            'to Swedish keyboard layout, but starts with US English language '
            'for neutrality.  Use if there is a single combined SKU for Nordic '
            'countries.'), 23, 'SE'),
    Region(
        'nz', 'xkb:us::eng', 'Pacific/Auckland', 'en-NZ', _KML.ANSI,
        'New Zealand', None, 24),
    Region(
        'ph', 'xkb:us::eng', 'Asia/Manila', 'en-US', _KML.ANSI,
        'Philippines', None, 25),
    Region(
        'ru', 'xkb:ru::rus', 'Europe/Moscow', 'ru', _KML.ANSI, 'Russia',
        'For R31+ only; R30 and earlier must use US keyboard for login',
        26),
    Region(
        'se', 'xkb:se::swe', 'Europe/Stockholm', 'sv', _KML.ISO, 'Sweden',
        (
            'Use this if there separate SKUs for Nordic countries (Sweden, '
            'Norway, and Denmark), or the device is only shipping to Sweden. '
            "If there is a single unified SKU, use 'nordic' instead."), 27),
    Region(
        'sg', 'xkb:us::eng', 'Asia/Singapore', 'en-GB', _KML.ANSI,
        'Singapore', None, 28),
    Region(
        'us', 'xkb:us::eng', 'America/Los_Angeles', 'en-US', _KML.ANSI,
        'United States', None, 29),
    Region(
        'jp', 'xkb:jp::jpn', 'Asia/Tokyo', 'ja', _KML.JIS, 'Japan', None,
        30),
    Region(
        'za', 'xkb:gb:extd:eng', 'Africa/Johannesburg', 'en-ZA',
        _KML.ISO, 'South Africa', None, 31),
    Region(
        'ng', 'xkb:us:intl:eng', 'Africa/Lagos', 'en-GB', _KML.ANSI,
        'Nigeria', None, 32),
    Region(
        'hk',
        ['xkb:us::eng', 'ime:zh-t:cangjie', 'ime:zh-t:quick',
         'ime:zh-t:array', 'ime:zh-t:dayi', 'ime:zh-t:zhuyin',
         'ime:zh-t:pinyin'], 'Asia/Hong_Kong',
        ['zh-TW', 'en-GB', 'zh-CN'], _KML.ANSI, 'Hong Kong', None, 33),
    Region(
        'gcc', ['xkb:us::eng', 'm17n:ar', 't13n:ar'], 'Asia/Riyadh',
        ['ar', 'en-GB'], _KML.ANSI, 'Gulf Cooperation Council (GCC)',
        (
            'GCC is a regional intergovernmental political and economic '
            'union consisting of all Arab states of the Persian Gulf except '
            'for Iraq. Its member states are the Islamic monarchies of '
            'Bahrain, Kuwait, Oman, Qatar, Saudi Arabia, and the United Arab '
            'Emirates.'), 34, 'SA'),
    Region(
        'cz', ['xkb:cz::cze', 'xkb:cz:qwerty:cze'], 'Europe/Prague',
        ['cs', 'en-GB'], _KML.ISO, 'Czech Republic', None, 35),
    Region(
        'th',
        ['xkb:us::eng', 'm17n:th', 'm17n:th_pattajoti', 'm17n:th_tis'],
        'Asia/Bangkok', ['th', 'en-GB'], _KML.ANSI, 'Thailand', None, 36),
    Region(
        'id', 'xkb:us::ind', 'Asia/Jakarta', ['id', 'en-GB'], _KML.ANSI,
        'Indonesia', None, 37),
    Region(
        'tw',
        ['xkb:us::eng', 'ime:zh-t:zhuyin', 'ime:zh-t:array',
         'ime:zh-t:dayi', 'ime:zh-t:cangjie', 'ime:zh-t:quick',
         'ime:zh-t:pinyin'], 'Asia/Taipei', ['zh-TW', 'en-US'],
        _KML.ANSI, 'Taiwan', None, 38),
    Region(
        'pl', 'xkb:pl::pol', 'Europe/Warsaw', ['pl', 'en-GB'],
        _KML.ANSI, 'Poland', None, 39),
    Region(
        'gr', ['xkb:us::eng', 'xkb:gr::gre', 't13n:el'], 'Europe/Athens',
        ['el', 'en-GB'], _KML.ANSI, 'Greece', None, 40),
    Region(
        'il', ['xkb:us::eng', 'xkb:il::heb', 't13n:he'], 'Asia/Jerusalem',
        ['he', 'en-US', 'ar'], _KML.ANSI, 'Israel', None, 41),
    Region(
        'pt', 'xkb:pt::por', 'Europe/Lisbon', ['pt-PT', 'en-GB'],
        _KML.ISO, 'Portugal', None, 42),
    Region(
        'ro', ['xkb:us::eng', 'xkb:ro::rum'], 'Europe/Bucharest',
        ['ro', 'hu', 'de', 'en-GB'], _KML.ISO, 'Romania', None, 43),
    Region(
        'kr', ['xkb:us::eng', 'ime:ko:hangul'], 'Asia/Seoul',
        ['ko', 'en-US'], _KML.KS, 'South Korea', None, 44),
    Region(
        'ae', 'xkb:us::eng', 'Asia/Dubai', 'ar', _KML.ANSI, 'UAE', None,
        45),
    Region(
        'za.us', 'xkb:us::eng', 'Africa/Johannesburg', 'en-ZA',
        _KML.ANSI, 'South Africa', None, 46),
    Region(
        'vn',
        ['xkb:us::eng', 'm17n:vi_telex', 'm17n:vi_vni', 'm17n:vi_viqr',
         'm17n:vi_tcvn'], 'Asia/Ho_Chi_Minh',
        ['vi', 'en-GB', 'en-US', 'fr', 'zh-TW'], _KML.ANSI, 'Vietnam',
        None, 47),
    Region(
        'at', ['xkb:de::ger', 'xkb:de:neo:ger'], 'Europe/Vienna',
        ['de', 'en-GB'], _KML.ISO, 'Austria', None, 48),
    Region(
        'sk', ['xkb:us::eng', 'xkb:sk::slo'], 'Europe/Bratislava',
        ['sk', 'hu', 'cs', 'en-GB'], _KML.ISO, 'Slovakia', None, 49),
    Region(
        'ch.usintl', 'xkb:us:intl:eng', 'Europe/Zurich', 'en-US',
        _KML.ANSI, 'Switzerland (US Intl)',
        'Switzerland with US International keyboard layout.', 50)]
"""A list of :py:class:`regions.Region` objects for
all **confirmed** regions.  A confirmed region is a region whose
properties are known to be correct and valid: all contents (locale / timezone /
keyboards) are supported by Chrome."""


UNCONFIRMED_REGIONS_LIST = [
    Region(
        'bd', 'xkb:bd::ben', 'Asia/Dhaka', ['bn-BD', 'en'], _KML.ANSI,
        'Bangladesh', None, 51),
    Region(
        'bf', 'xkb:bf::fra', 'Africa/Ouagadougou', 'fr-BF', _KML.ANSI,
        'Burkina Faso', None, 52),
    Region(
        'bg', ['xkb:bg::bul', 'xkb:bg:phonetic:bul'], 'Europe/Sofia',
        ['bg', 'tr', 'en-GB'], _KML.ANSI, 'Bulgaria', None, 53),
    Region(
        'ba', 'xkb:ba::bos', 'Europe/Sarajevo', ['bs', 'hr-BA', 'sr-BA'],
        _KML.ANSI, 'Bosnia and Herzegovina', None, 54),
    Region(
        'bb', 'xkb:bb::eng', 'America/Barbados', 'en-BB', _KML.ANSI,
        'Barbados', None, 55),
    Region(
        'wf', 'xkb:us::eng', 'Pacific/Wallis', ['wls', 'fud', 'fr-WF'],
        _KML.ANSI, 'Wallis and Futuna', None, 56),
    Region(
        'bl', 'xkb:bl::fra', 'America/St_Barthelemy', 'fr', _KML.ANSI,
        'Saint Barthelemy', None, 57),
    Region(
        'bm', 'xkb:bm::eng', 'Atlantic/Bermuda', ['en-BM', 'pt'],
        _KML.ANSI, 'Bermuda', None, 58),
    Region(
        'bn', 'xkb:bn::msa', 'Asia/Brunei', ['ms-BN', 'en-BN'],
        _KML.ANSI, 'Brunei', None, 59),
    Region(
        'bo', 'xkb:bo::spa', 'America/La_Paz', ['es-BO', 'qu', 'ay'],
        _KML.ANSI, 'Bolivia', None, 60),
    Region(
        'bh', 'xkb:bh::ara', 'Asia/Bahrain', ['ar-BH', 'en', 'fa', 'ur'],
        _KML.ANSI, 'Bahrain', None, 61),
    Region(
        'bi', 'xkb:bi::fra', 'Africa/Bujumbura', ['fr-BI', 'rn'],
        _KML.ANSI, 'Burundi', None, 62),
    Region(
        'bj', 'xkb:bj::fra', 'Africa/Porto-Novo', 'fr-BJ', _KML.ANSI,
        'Benin', None, 63),
    Region(
        'bt', 'xkb:bt::dzo', 'Asia/Thimphu', 'dz', _KML.ANSI, 'Bhutan',
        None, 64),
    Region(
        'jm', 'xkb:jm::eng', 'America/Jamaica', 'en-JM', _KML.ANSI,
        'Jamaica', None, 65),
    Region(
        'bw', 'xkb:bw::eng', 'Africa/Gaborone', ['en-BW', 'tn-BW'],
        _KML.ANSI, 'Botswana', None, 66),
    Region(
        'ws', 'xkb:ws::smo', 'Pacific/Apia', ['sm', 'en-WS'], _KML.ANSI,
        'Samoa', None, 67),
    Region(
        'bq', 'xkb:bq::nld', 'America/Kralendijk', ['nl', 'pap', 'en'],
        _KML.ANSI, 'Bonaire, Saint Eustatius and Saba ', None, 68),
    Region(
        'bs', 'xkb:bs::eng', 'America/Nassau', 'en-BS', _KML.ANSI,
        'Bahamas', None, 69),
    Region(
        'je', 'xkb:je::eng', 'Europe/Jersey', ['en', 'pt'], _KML.ANSI,
        'Jersey', None, 70),
    Region(
        'by', 'xkb:by::bel', 'Europe/Minsk', ['be', 'ru'], _KML.ANSI,
        'Belarus', None, 71),
    Region(
        'bz', 'xkb:bz::eng', 'America/Belize', ['en-BZ', 'es'],
        _KML.ANSI, 'Belize', None, 72),
    Region(
        'rw', 'xkb:rw::kin', 'Africa/Kigali',
        ['rw', 'en-RW', 'fr-RW', 'sw'], _KML.ANSI, 'Rwanda', None, 73),
    Region(
        'rs', 'xkb:rs::srp', 'Europe/Belgrade', ['sr', 'hu', 'bs', 'rom'],
        _KML.ANSI, 'Serbia', None, 74),
    Region(
        'tl', 'xkb:us::eng', 'Asia/Dili', ['tet', 'pt-TL', 'id', 'en'],
        _KML.ANSI, 'East Timor', None, 75),
    Region(
        're', 'xkb:re::fra', 'Indian/Reunion', 'fr-RE', _KML.ANSI,
        'Reunion', None, 76),
    Region(
        'tm', 'xkb:tm::tuk', 'Asia/Ashgabat', ['tk', 'ru', 'uz'],
        _KML.ANSI, 'Turkmenistan', None, 77),
    Region(
        'tj', 'xkb:tj::tgk', 'Asia/Dushanbe', ['tg', 'ru'], _KML.ANSI,
        'Tajikistan', None, 78),
    Region(
        'tk', 'xkb:us::eng', 'Pacific/Fakaofo', ['tkl', 'en-TK'],
        _KML.ANSI, 'Tokelau', None, 79),
    Region(
        'gw', 'xkb:gw::por', 'Africa/Bissau', ['pt-GW', 'pov'],
        _KML.ANSI, 'Guinea-Bissau', None, 80),
    Region(
        'gu', 'xkb:gu::eng', 'Pacific/Guam', ['en-GU', 'ch-GU'],
        _KML.ANSI, 'Guam', None, 81),
    Region(
        'gt', 'xkb:gt::spa', 'America/Guatemala', 'es-GT', _KML.ANSI,
        'Guatemala', None, 82),
    Region(
        'gs', 'xkb:gs::eng', 'Atlantic/South_Georgia', 'en', _KML.ANSI,
        'South Georgia and the South Sandwich Islands', None, 83),
    Region(
        'gq', 'xkb:gq::spa', 'Africa/Malabo', ['es-GQ', 'fr'],
        _KML.ANSI, 'Equatorial Guinea', None, 84),
    Region(
        'gp', 'xkb:gp::fra', 'America/Guadeloupe', 'fr-GP', _KML.ANSI,
        'Guadeloupe', None, 85),
    Region(
        'gy', 'xkb:gy::eng', 'America/Guyana', 'en-GY', _KML.ANSI,
        'Guyana', None, 86),
    Region(
        'gg', 'xkb:gg::eng', 'Europe/Guernsey', ['en', 'fr'], _KML.ANSI,
        'Guernsey', None, 87),
    Region(
        'gf', 'xkb:gf::fra', 'America/Cayenne', 'fr-GF', _KML.ANSI,
        'French Guiana', None, 88),
    Region(
        'ge', 'xkb:ge::geo', 'Asia/Tbilisi', 'ka', _KML.ANSI, 'Georgia', None,
        89),
    Region(
        'gd', 'xkb:gd::eng', 'America/Grenada', 'en-GD', _KML.ANSI,
        'Grenada', None, 90),
    Region(
        'ga', 'xkb:ga::fra', 'Africa/Libreville', 'fr-GA', _KML.ANSI,
        'Gabon', None, 91),
    Region(
        'sv', 'xkb:sv::spa', 'America/El_Salvador', 'es-SV', _KML.ANSI,
        'El Salvador', None, 92),
    Region(
        'gn', 'xkb:gn::fra', 'Africa/Conakry', 'fr-GN', _KML.ANSI,
        'Guinea', None, 93),
    Region(
        'gm', 'xkb:gm::eng', 'Africa/Banjul',
        ['en-GM', 'mnk', 'wof', 'wo', 'ff'], _KML.ANSI, 'Gambia', None, 94),
    Region(
        'gl', 'xkb:gl::kal',
        ['America/Godthab', 'America/Danmarkshavn',
         'America/Scoresbysund', 'America/Thule'], ['kl', 'da-GL', 'en'],
        _KML.ANSI, 'Greenland', None, 95),
    Region(
        'gi', 'xkb:gi::eng', 'Europe/Gibraltar',
        ['en-GI', 'es', 'it', 'pt'], _KML.ANSI, 'Gibraltar', None, 96),
    Region(
        'gh', 'xkb:gh::eng', 'Africa/Accra', ['en-GH', 'ak', 'ee', 'tw'],
        _KML.ANSI, 'Ghana', None, 97),
    Region(
        'om', 'xkb:om::ara', 'Asia/Muscat', ['ar-OM', 'en', 'bal', 'ur'],
        _KML.ANSI, 'Oman', None, 98),
    Region(
        'tn', 'xkb:tn::ara', 'Africa/Tunis', ['ar-TN', 'fr'], _KML.ANSI,
        'Tunisia', None, 99),
    Region(
        'jo', 'xkb:jo::ara', 'Asia/Amman', ['ar-JO', 'en'], _KML.ANSI,
        'Jordan', None, 100),
    Region(
        'hr', 'xkb:hr::scr', 'Europe/Zagreb', ['hr', 'en-GB'],
        _KML.ISO, 'Croatia', None, 101),
    Region(
        'ht', 'xkb:ht::hat', 'America/Port-au-Prince', ['ht', 'fr-HT'],
        _KML.ANSI, 'Haiti', None, 102),
    Region(
        'hu', ['xkb:us::eng', 'xkb:hu::hun'], 'Europe/Budapest',
        ['hu', 'en-GB'], _KML.ISO, 'Hungary', None, 103),
    Region(
        'hn', 'xkb:hn::spa', 'America/Tegucigalpa', 'es-HN', _KML.ANSI,
        'Honduras', None, 104),
    Region(
        've', 'xkb:ve::spa', 'America/Caracas', 'es-VE', _KML.ANSI,
        'Venezuela', None, 105),
    Region(
        'pr', 'xkb:pr::eng', 'America/Puerto_Rico', ['en-PR', 'es-PR'],
        _KML.ANSI, 'Puerto Rico', None, 106),
    Region(
        'ps', 'xkb:ps::ara', ['Asia/Gaza', 'Asia/Hebron'], 'ar-PS',
        _KML.ANSI, 'Palestinian Territory', None, 107),
    Region(
        'pw', 'xkb:us::eng', 'Pacific/Palau',
        ['pau', 'sov', 'en-PW', 'tox', 'ja', 'fil', 'zh'], _KML.ANSI,
        'Palau', None, 108),
    Region(
        'sj', 'xkb:sj::nor', 'Arctic/Longyearbyen', ['no', 'ru'],
        _KML.ANSI, 'Svalbard and Jan Mayen', None, 109),
    Region(
        'py', 'xkb:py::spa', 'America/Asuncion', ['es-PY', 'gn'],
        _KML.ANSI, 'Paraguay', None, 110),
    Region(
        'iq', 'xkb:iq::ara', 'Asia/Baghdad', ['ar-IQ', 'ku', 'hy'],
        _KML.ANSI, 'Iraq', None, 111),
    Region(
        'pa', 'xkb:pa::spa', 'America/Panama', ['es-PA', 'en'],
        _KML.ANSI, 'Panama', None, 112),
    Region(
        'pf', 'xkb:pf::fra',
        ['Pacific/Tahiti', 'Pacific/Marquesas', 'Pacific/Gambier'],
        ['fr-PF', 'ty'], _KML.ANSI, 'French Polynesia', None, 113),
    Region(
        'pg', 'xkb:pg::eng',
        ['Pacific/Port_Moresby', 'Pacific/Bougainville'],
        ['en-PG', 'ho', 'meu', 'tpi'], _KML.ANSI, 'Papua New Guinea', None,
        114),
    Region(
        'pe', 'xkb:pe::spa', 'America/Lima', ['es-PE', 'qu', 'ay'],
        _KML.ANSI, 'Peru', None, 115),
    Region(
        'pk', 'xkb:pk::urd', 'Asia/Karachi',
        ['ur-PK', 'en-PK', 'pa', 'sd', 'ps', 'brh'], _KML.ANSI,
        'Pakistan', None, 116),
    Region(
        'pn', 'xkb:pn::eng', 'Pacific/Pitcairn', 'en-PN', _KML.ANSI,
        'Pitcairn', None, 117),
    Region(
        'pm', 'xkb:pm::fra', 'America/Miquelon', 'fr-PM', _KML.ANSI,
        'Saint Pierre and Miquelon', None, 118),
    Region(
        'zm', 'xkb:zm::eng', 'Africa/Lusaka',
        ['en-ZM', 'bem', 'loz', 'lun', 'lue', 'ny', 'toi'], _KML.ANSI,
        'Zambia', None, 119),
    Region(
        'eh', 'xkb:eh::ara', 'Africa/El_Aaiun', ['ar', 'mey'],
        _KML.ANSI, 'Western Sahara', None, 120),
    Region(
        'ee', 'xkb:ee::est', 'Europe/Tallinn', ['et', 'ru', 'en-GB'], _KML.ISO,
        'Estonia', None, 121),
    Region(
        'eg', 'xkb:eg::ara', 'Africa/Cairo', ['ar-EG', 'en', 'fr'],
        _KML.ANSI, 'Egypt', None, 122),
    Region(
        'ec', 'xkb:ec::spa', ['America/Guayaquil', 'Pacific/Galapagos'],
        'es-EC', _KML.ANSI, 'Ecuador', None, 123),
    Region(
        'sb', 'xkb:sb::eng', 'Pacific/Guadalcanal', ['en-SB', 'tpi'],
        _KML.ANSI, 'Solomon Islands', None, 124),
    Region(
        'et', 'xkb:et::amh', 'Africa/Addis_Ababa',
        ['am', 'en-ET', 'om-ET', 'ti-ET', 'so-ET', 'sid'], _KML.ANSI,
        'Ethiopia', None, 125),
    Region(
        'so', 'xkb:so::som', 'Africa/Mogadishu',
        ['so-SO', 'ar-SO', 'it', 'en-SO'], _KML.ANSI, 'Somalia', None, 126),
    Region(
        'zw', 'xkb:zw::eng', 'Africa/Harare', ['en-ZW', 'sn', 'nr', 'nd'],
        _KML.ANSI, 'Zimbabwe', None, 127),
    Region(
        'sa', 'xkb:us::eng', 'Asia/Riyadh', 'ar-SA', _KML.ANSI,
        'Saudi Arabia', None, 128),
    Region(
        'er', 'xkb:er::aar', 'Africa/Asmara',
        ['aa-ER', 'ar', 'tig', 'kun', 'ti-ER'], _KML.ANSI, 'Eritrea', None,
        129),
    Region(
        'me', 'xkb:me::srp', 'Europe/Podgorica',
        ['sr', 'hu', 'bs', 'sq', 'hr', 'rom'], _KML.ANSI, 'Montenegro',
        None, 130),
    Region(
        'md', 'xkb:md::ron', 'Europe/Chisinau', ['ro', 'ru', 'gag', 'tr'],
        _KML.ANSI, 'Moldova', None, 131),
    Region(
        'mg', 'xkb:mg::fra', 'Indian/Antananarivo', ['fr-MG', 'mg'],
        _KML.ANSI, 'Madagascar', None, 132),
    Region(
        'mf', 'xkb:mf::fra', 'America/Marigot', 'fr', _KML.ANSI,
        'Saint Martin', None, 133),
    Region(
        'ma', 'xkb:ma::ara', 'Africa/Casablanca', ['ar-MA', 'fr'],
        _KML.ANSI, 'Morocco', None, 134),
    Region(
        'mc', 'xkb:mc::fra', 'Europe/Monaco', ['fr-MC', 'en', 'it'],
        _KML.ANSI, 'Monaco', None, 135),
    Region(
        'uz', 'xkb:uz::uzb', ['Asia/Samarkand', 'Asia/Tashkent'],
        ['uz', 'ru', 'tg'], _KML.ANSI, 'Uzbekistan', None, 136),
    Region(
        'mm', 'xkb:mm::mya', 'Asia/Rangoon', 'my', _KML.ANSI, 'Myanmar',
        None, 137),
    Region(
        'ml', 'xkb:ml::fra', 'Africa/Bamako', ['fr-ML', 'bm'],
        _KML.ANSI, 'Mali', None, 138),
    Region(
        'mo', 'xkb:mo::zho', 'Asia/Macau', ['zh', 'zh-MO', 'pt'],
        _KML.ANSI, 'Macao', None, 139),
    Region(
        'mn', 'xkb:mn::mon',
        ['Asia/Ulaanbaatar', 'Asia/Hovd', 'Asia/Choibalsan'],
        ['mn', 'ru'], _KML.ANSI, 'Mongolia', None, 140),
    Region(
        'mh', 'xkb:mh::mah', ['Pacific/Majuro', 'Pacific/Kwajalein'],
        ['mh', 'en-MH'], _KML.ANSI, 'Marshall Islands', None, 141),
    Region(
        'mk', 'xkb:mk::mkd', 'Europe/Skopje',
        ['mk', 'sq', 'tr', 'rmm', 'sr'], _KML.ANSI, 'Macedonia', None, 142),
    Region(
        'mu', 'xkb:mu::eng', 'Indian/Mauritius', ['en-MU', 'bho', 'fr'],
        _KML.ANSI, 'Mauritius', None, 143),
    Region(
        'mt', ['xkb:us::eng', 'xkb:mt::mlt'], 'Europe/Malta', ['mt', 'en-GB'],
        _KML.ISO, 'Malta', None, 144),
    Region(
        'mw', 'xkb:mw::nya', 'Africa/Blantyre',
        ['ny', 'yao', 'tum', 'swk'], _KML.ANSI, 'Malawi', None, 145),
    Region(
        'mv', 'xkb:mv::div', 'Indian/Maldives', ['dv', 'en'], _KML.ANSI,
        'Maldives', None, 146),
    Region(
        'mq', 'xkb:mq::fra', 'America/Martinique', 'fr-MQ', _KML.ANSI,
        'Martinique', None, 147),
    Region(
        'mp', 'xkb:us::eng', 'Pacific/Saipan',
        ['fil', 'tl', 'zh', 'ch-MP', 'en-MP'], _KML.ANSI,
        'Northern Mariana Islands', None, 148),
    Region(
        'ms', 'xkb:ms::eng', 'America/Montserrat', 'en-MS', _KML.ANSI,
        'Montserrat', None, 149),
    Region(
        'mr', 'xkb:mr::ara', 'Africa/Nouakchott',
        ['ar-MR', 'fuc', 'snk', 'fr', 'mey', 'wo'], _KML.ANSI,
        'Mauritania', None, 150),
    Region(
        'im', 'xkb:im::eng', 'Europe/Isle_of_Man', ['en', 'gv'],
        _KML.ANSI, 'Isle of Man', None, 151),
    Region(
        'ug', 'xkb:ug::eng', 'Africa/Kampala',
        ['en-UG', 'lg', 'sw', 'ar'], _KML.ANSI, 'Uganda', None, 152),
    Region(
        'tz', 'xkb:tz::swa', 'Africa/Dar_es_Salaam',
        ['sw-TZ', 'en', 'ar'], _KML.ANSI, 'Tanzania', None, 153),
    Region(
        'mx', 'xkb:mx::spa',
        ['America/Mexico_City', 'America/Cancun', 'America/Merida',
         'America/Monterrey', 'America/Matamoros', 'America/Mazatlan',
         'America/Chihuahua', 'America/Ojinaga', 'America/Hermosillo',
         'America/Tijuana', 'America/Santa_Isabel',
         'America/Bahia_Banderas'], 'es-MX', _KML.ANSI, 'Mexico', None, 154),
    Region(
        'io', 'xkb:io::eng', 'Indian/Chagos', 'en-IO', _KML.ANSI,
        'British Indian Ocean Territory', None, 155),
    Region(
        'sh', 'xkb:sh::eng', 'Atlantic/St_Helena', 'en-SH', _KML.ANSI,
        'Saint Helena', None, 156),
    Region(
        'fj', 'xkb:fj::eng', 'Pacific/Fiji', ['en-FJ', 'fj'], _KML.ANSI,
        'Fiji', None, 157),
    Region(
        'fk', 'xkb:fk::eng', 'Atlantic/Stanley', 'en-FK', _KML.ANSI,
        'Falkland Islands', None, 158),
    Region(
        'fm', 'xkb:fm::eng',
        ['Pacific/Chuuk', 'Pacific/Pohnpei', 'Pacific/Kosrae'],
        ['en-FM', 'chk', 'pon', 'yap', 'kos', 'uli', 'woe', 'nkr', 'kpg'],
        _KML.ANSI, 'Micronesia', None, 159),
    Region(
        'fo', 'xkb:fo::fao', 'Atlantic/Faroe', ['fo', 'da-FO'],
        _KML.ANSI, 'Faroe Islands', None, 160),
    Region(
        'ni', 'xkb:ni::spa', 'America/Managua', ['es-NI', 'en'],
        _KML.ANSI, 'Nicaragua', None, 161),
    Region(
        'no', 'xkb:no::nor', 'Europe/Oslo',
        ['no', 'nb', 'nn', 'se', 'fi'], _KML.ISO, 'Norway', None, 162),
    Region(
        'na', 'xkb:na::eng', 'Africa/Windhoek',
        ['en-NA', 'af', 'de', 'hz', 'naq'], _KML.ANSI, 'Namibia', None, 163),
    Region(
        'vu', 'xkb:vu::bis', 'Pacific/Efate', ['bi', 'en-VU', 'fr-VU'],
        _KML.ANSI, 'Vanuatu', None, 164),
    Region(
        'nc', 'xkb:nc::fra', 'Pacific/Noumea', 'fr-NC', _KML.ANSI,
        'New Caledonia', None, 165),
    Region(
        'ne', 'xkb:ne::fra', 'Africa/Niamey',
        ['fr-NE', 'ha', 'kr', 'dje'], _KML.ANSI, 'Niger', None, 166),
    Region(
        'nf', 'xkb:nf::eng', 'Pacific/Norfolk', 'en-NF', _KML.ANSI,
        'Norfolk Island', None, 167),
    Region(
        'np', 'xkb:np::nep', 'Asia/Kathmandu', ['ne', 'en'], _KML.ANSI,
        'Nepal', None, 168),
    Region(
        'nr', 'xkb:nr::nau', 'Pacific/Nauru', ['na', 'en-NR'],
        _KML.ANSI, 'Nauru', None, 169),
    Region(
        'nu', 'xkb:us::eng', 'Pacific/Niue', ['niu', 'en-NU'],
        _KML.ANSI, 'Niue', None, 170),
    Region(
        'ck', 'xkb:ck::eng', 'Pacific/Rarotonga', ['en-CK', 'mi'],
        _KML.ANSI, 'Cook Islands', None, 171),
    Region(
        'ci', 'xkb:ci::fra', 'Africa/Abidjan', 'fr-CI', _KML.ANSI,
        'Ivory Coast', None, 172),
    Region(
        'co', 'xkb:co::spa', 'America/Bogota', 'es-CO', _KML.ANSI,
        'Colombia', None, 173),
    Region(
        'cn', 'xkb:us::eng', 'Asia/Shanghai', 'zh-CN', _KML.ANSI, 'China',
        None, 174),
    Region(
        'cm', 'xkb:cm::eng', 'Africa/Douala', ['en-CM', 'fr-CM'],
        _KML.ANSI, 'Cameroon', None, 175),
    Region(
        'cl', 'xkb:cl::spa', ['America/Santiago', 'Pacific/Easter'],
        'es-CL', _KML.ANSI, 'Chile', None, 176),
    Region(
        'cc', 'xkb:cc::msa', 'Indian/Cocos', ['ms-CC', 'en'], _KML.ANSI,
        'Cocos Islands', None, 177),
    Region(
        'cg', 'xkb:cg::fra', 'Africa/Brazzaville',
        ['fr-CG', 'kg', 'ln-CG'], _KML.ANSI, 'Republic of the Congo', None,
        178),
    Region(
        'cf', 'xkb:cf::fra', 'Africa/Bangui', ['fr-CF', 'sg', 'ln', 'kg'],
        _KML.ANSI, 'Central African Republic', None, 179),
    Region(
        'cd', 'xkb:cd::fra', ['Africa/Kinshasa', 'Africa/Lubumbashi'],
        ['fr-CD', 'ln', 'kg'], _KML.ANSI,
        'Democratic Republic of the Congo', None, 180),
    Region(
        'cy', 'xkb:cy::ell', 'Asia/Nicosia', ['el-CY', 'tr-CY', 'en'],
        _KML.ANSI, 'Cyprus', None, 181),
    Region(
        'cx', 'xkb:cx::eng', 'Indian/Christmas', ['en', 'zh', 'ms-CC'],
        _KML.ANSI, 'Christmas Island', None, 182),
    Region(
        'cr', 'xkb:cr::spa', 'America/Costa_Rica', ['es-CR', 'en'],
        _KML.ANSI, 'Costa Rica', None, 183),
    Region(
        'cw', 'xkb:cw::nld', 'America/Curacao', ['nl', 'pap'],
        _KML.ANSI, 'Curacao', None, 184),
    Region(
        'cv', 'xkb:cv::por', 'Atlantic/Cape_Verde', 'pt-CV', _KML.ANSI,
        'Cape Verde', None, 185),
    Region(
        'cu', 'xkb:cu::spa', 'America/Havana', 'es-CU', _KML.ANSI, 'Cuba',
        None, 186),
    Region(
        'sz', 'xkb:sz::eng', 'Africa/Mbabane', ['en-SZ', 'ss-SZ'],
        _KML.ANSI, 'Swaziland', None, 187),
    Region(
        'sy', 'xkb:sy::ara', 'Asia/Damascus',
        ['ar-SY', 'ku', 'hy', 'arc', 'fr', 'en'], _KML.ANSI, 'Syria', None,
        188),
    Region(
        'sx', 'xkb:sx::nld', 'America/Lower_Princes', ['nl', 'en'],
        _KML.ANSI, 'Sint Maarten', None, 189),
    Region(
        'kg', 'xkb:kg::kir', 'Asia/Bishkek', ['ky', 'uz', 'ru'],
        _KML.ANSI, 'Kyrgyzstan', None, 190),
    Region(
        'ke', 'xkb:ke::eng', 'Africa/Nairobi', ['en-KE', 'sw-KE'],
        _KML.ANSI, 'Kenya', None, 191),
    Region(
        'ss', 'xkb:ss::eng', 'Africa/Juba', 'en', _KML.ANSI,
        'South Sudan', None, 192),
    Region(
        'sr', 'xkb:sr::nld', 'America/Paramaribo',
        ['nl-SR', 'en', 'srn', 'hns', 'jv'], _KML.ANSI, 'Suriname', None, 193),
    Region(
        'ki', 'xkb:ki::eng',
        ['Pacific/Tarawa', 'Pacific/Enderbury', 'Pacific/Kiritimati'],
        ['en-KI', 'gil'], _KML.ANSI, 'Kiribati', None, 194),
    Region(
        'kh', 'xkb:kh::khm', 'Asia/Phnom_Penh', ['km', 'fr', 'en'],
        _KML.ANSI, 'Cambodia', None, 195),
    Region(
        'kn', 'xkb:kn::eng', 'America/St_Kitts', 'en-KN', _KML.ANSI,
        'Saint Kitts and Nevis', None, 196),
    Region(
        'km', 'xkb:km::ara', 'Indian/Comoro', ['ar', 'fr-KM'],
        _KML.ANSI, 'Comoros', None, 197),
    Region(
        'st', 'xkb:st::por', 'Africa/Sao_Tome', 'pt-ST', _KML.ANSI,
        'Sao Tome and Principe', None, 198),
    Region(
        'si', 'xkb:si::slv', 'Europe/Ljubljana',
        ['sl', 'hu', 'it', 'sr', 'de', 'hr', 'en-GB'], _KML.ISO,
        'Slovenia', None, 199),
    Region(
        'kp', 'xkb:kp::kor', 'Asia/Pyongyang', 'ko-KP', _KML.ANSI,
        'North Korea', None, 200),
    Region(
        'kw', 'xkb:kw::ara', 'Asia/Kuwait', ['ar-KW', 'en'], _KML.ANSI,
        'Kuwait', None, 201),
    Region(
        'sn', 'xkb:sn::fra', 'Africa/Dakar',
        ['fr-SN', 'wo', 'fuc', 'mnk'], _KML.ANSI, 'Senegal', None, 202),
    Region(
        'sm', 'xkb:sm::ita', 'Europe/San_Marino', 'it-SM', _KML.ANSI,
        'San Marino', None, 203),
    Region(
        'sl', 'xkb:sl::eng', 'Africa/Freetown', ['en-SL', 'men', 'tem'],
        _KML.ANSI, 'Sierra Leone', None, 204),
    Region(
        'sc', 'xkb:sc::eng', 'Indian/Mahe', ['en-SC', 'fr-SC'],
        _KML.ANSI, 'Seychelles', None, 205),
    Region(
        'kz', 'xkb:kz::kaz',
        ['Asia/Almaty', 'Asia/Qyzylorda', 'Asia/Aqtobe', 'Asia/Aqtau',
         'Asia/Oral'], ['kk', 'ru'], _KML.ANSI, 'Kazakhstan', None, 206),
    Region(
        'ky', 'xkb:ky::eng', 'America/Cayman', 'en-KY', _KML.ANSI,
        'Cayman Islands', None, 207),
    Region(
        'sd', 'xkb:sd::ara', 'Africa/Khartoum', ['ar-SD', 'en', 'fia'],
        _KML.ANSI, 'Sudan', None, 208),
    Region(
        'do', 'xkb:do::spa', 'America/Santo_Domingo', 'es-DO',
        _KML.ANSI, 'Dominican Republic', None, 209),
    Region(
        'dm', 'xkb:dm::eng', 'America/Dominica', 'en-DM', _KML.ANSI,
        'Dominica', None, 210),
    Region(
        'dj', 'xkb:dj::fra', 'Africa/Djibouti',
        ['fr-DJ', 'ar', 'so-DJ', 'aa'], _KML.ANSI, 'Djibouti', None, 211),
    Region(
        'dk', 'xkb:dk::dan', 'Europe/Copenhagen',
        ['da-DK', 'en', 'fo', 'de-DK'], _KML.ISO, 'Denmark', None, 212),
    Region(
        'vg', 'xkb:vg::eng', 'America/Tortola', 'en-VG', _KML.ANSI,
        'British Virgin Islands', None, 213),
    Region(
        'ye', 'xkb:ye::ara', 'Asia/Aden', 'ar-YE', _KML.ANSI, 'Yemen',
        None, 214),
    Region(
        'dz', 'xkb:dz::ara', 'Africa/Algiers', 'ar-DZ', _KML.ANSI,
        'Algeria', None, 215),
    Region(
        'uy', 'xkb:uy::spa', 'America/Montevideo', 'es-UY', _KML.ANSI,
        'Uruguay', None, 216),
    Region(
        'yt', 'xkb:yt::fra', 'Indian/Mayotte', 'fr-YT', _KML.ANSI,
        'Mayotte', None, 217),
    Region(
        'um', 'xkb:um::eng',
        ['Pacific/Johnston', 'Pacific/Midway', 'Pacific/Wake'], 'en-UM',
        _KML.ANSI, 'United States Minor Outlying Islands', None, 218),
    Region(
        'lb', 'xkb:lb::ara', 'Asia/Beirut',
        ['ar-LB', 'fr-LB', 'en', 'hy'], _KML.ANSI, 'Lebanon', None, 219),
    Region(
        'lc', 'xkb:lc::eng', 'America/St_Lucia', 'en-LC', _KML.ANSI,
        'Saint Lucia', None, 220),
    Region(
        'la', 'xkb:la::lao', 'Asia/Vientiane', ['lo', 'fr', 'en'],
        _KML.ANSI, 'Laos', None, 221),
    Region(
        'tv', 'xkb:us::eng', 'Pacific/Funafuti',
        ['tvl', 'en', 'sm', 'gil'], _KML.ANSI, 'Tuvalu', None, 222),
    Region(
        'tt', 'xkb:tt::eng', 'America/Port_of_Spain',
        ['en-TT', 'hns', 'fr', 'es', 'zh'], _KML.ANSI,
        'Trinidad and Tobago', None, 223),
    Region(
        'tr', 'xkb:tr::tur', 'Europe/Istanbul',
        ['tr', 'ku', 'diq', 'az', 'av'], _KML.ISO, 'Turkey', None, 224),
    Region(
        'lk', 'xkb:lk::sin', 'Asia/Colombo', ['si', 'ta', 'en'],
        _KML.ANSI, 'Sri Lanka', None, 225),
    Region(
        'li', 'xkb:ch::ger', 'Europe/Vaduz', ['de', 'en-GB'], _KML.ISO,
        'Liechtenstein', None, 226),
    Region(
        'lv', 'xkb:lv:apostrophe:lav', 'Europe/Riga',
        ['lv', 'lt', 'ru', 'en-GB'], _KML.ISO, 'Latvia', None, 227),
    Region(
        'to', 'xkb:to::ton', 'Pacific/Tongatapu', ['to', 'en-TO'],
        _KML.ANSI, 'Tonga', None, 228),
    Region(
        'lt', 'xkb:lt::lit', 'Europe/Vilnius', ['lt', 'ru', 'pl', 'en-GB'],
        _KML.ISO, 'Lithuania', None, 229),
    Region(
        'lu', 'xkb:lu::ltz', 'Europe/Luxembourg',
        ['lb', 'de-LU', 'fr-LU'], _KML.ANSI, 'Luxembourg', None, 230),
    Region(
        'lr', 'xkb:lr::eng', 'Africa/Monrovia', 'en-LR', _KML.ANSI,
        'Liberia', None, 231),
    Region(
        'ls', 'xkb:ls::eng', 'Africa/Maseru', ['en-LS', 'st', 'zu', 'xh'],
        _KML.ANSI, 'Lesotho', None, 232),
    Region(
        'tf', 'xkb:tf::fra', 'Indian/Kerguelen', 'fr', _KML.ANSI,
        'French Southern Territories', None, 233),
    Region(
        'tg', 'xkb:tg::fra', 'Africa/Lome',
        ['fr-TG', 'ee', 'hna', 'kbp', 'dag', 'ha'], _KML.ANSI, 'Togo',
        None, 234),
    Region(
        'td', 'xkb:td::fra', 'Africa/Ndjamena', ['fr-TD', 'ar-TD', 'sre'],
        _KML.ANSI, 'Chad', None, 235),
    Region(
        'tc', 'xkb:tc::eng', 'America/Grand_Turk', 'en-TC', _KML.ANSI,
        'Turks and Caicos Islands', None, 236),
    Region(
        'ly', 'xkb:ly::ara', 'Africa/Tripoli', ['ar-LY', 'it', 'en'],
        _KML.ANSI, 'Libya', None, 237),
    Region(
        'va', 'xkb:va::lat', 'Europe/Vatican', ['la', 'it', 'fr'],
        _KML.ANSI, 'Vatican', None, 238),
    Region(
        'vc', 'xkb:vc::eng', 'America/St_Vincent', ['en-VC', 'fr'],
        _KML.ANSI, 'Saint Vincent and the Grenadines', None, 239),
    Region(
        'ad', 'xkb:ad::cat', 'Europe/Andorra', 'ca', _KML.ANSI, 'Andorra',
        None, 240),
    Region(
        'ag', 'xkb:ag::eng', 'America/Antigua', 'en-AG', _KML.ANSI,
        'Antigua and Barbuda', None, 241),
    Region(
        'af', 'xkb:af::fas', 'Asia/Kabul', ['fa-AF', 'ps', 'uz-AF', 'tk'],
        _KML.ANSI, 'Afghanistan', None, 242),
    Region(
        'ai', 'xkb:ai::eng', 'America/Anguilla', 'en-AI', _KML.ANSI,
        'Anguilla', None, 243),
    Region(
        'vi', 'xkb:vi::eng', 'America/St_Thomas', 'en-VI', _KML.ANSI,
        'U.S. Virgin Islands', None, 244),
    Region(
        'is', 'xkb:is::ice', 'Atlantic/Reykjavik',
        ['is', 'en-GB', 'da', 'de'], _KML.ISO, 'Iceland', None, 245),
    Region(
        'ir', 'xkb:ir::fas', 'Asia/Tehran', ['fa-IR', 'ku'], _KML.ANSI,
        'Iran', None, 246),
    Region(
        'am', 'xkb:am::hye', 'Asia/Yerevan', 'hy', _KML.ANSI, 'Armenia',
        None, 247),
    Region(
        'al', 'xkb:al::sqi', 'Europe/Tirane', ['sq', 'el'], _KML.ANSI,
        'Albania', None, 248),
    Region(
        'ao', 'xkb:ao::por', 'Africa/Luanda', 'pt-AO', _KML.ANSI,
        'Angola', None, 249),
    Region(
        'as', 'xkb:as::eng', 'Pacific/Pago_Pago', ['en-AS', 'sm', 'to'],
        _KML.ANSI, 'American Samoa', None, 250),
    Region(
        'ar', 'xkb:ar::spa',
        ['America/Argentina/Buenos_Aires', 'America/Argentina/Cordoba',
         'America/Argentina/Salta', 'America/Argentina/Jujuy',
         'America/Argentina/Tucuman', 'America/Argentina/Catamarca',
         'America/Argentina/La_Rioja', 'America/Argentina/San_Juan',
         'America/Argentina/Mendoza', 'America/Argentina/San_Luis',
         'America/Argentina/Rio_Gallegos', 'America/Argentina/Ushuaia'],
        ['es-AR', 'en', 'it', 'de', 'fr', 'gn'], _KML.ANSI, 'Argentina',
        None, 251),
    Region(
        'aw', 'xkb:aw::nld', 'America/Aruba', ['nl-AW', 'es', 'en'],
        _KML.ANSI, 'Aruba', None, 252),
    Region(
        'ax', 'xkb:ax::swe', 'Europe/Mariehamn', 'sv-AX', _KML.ANSI,
        'Aland Islands', None, 253),
    Region(
        'az', 'xkb:az::aze', 'Asia/Baku', ['az', 'ru', 'hy'], _KML.ANSI,
        'Azerbaijan', None, 254),
    Region(
        'ua', 'xkb:ua::ukr',
        ['Europe/Kiev', 'Europe/Uzhgorod', 'Europe/Zaporozhye'],
        ['uk', 'ru-UA', 'rom', 'pl', 'hu'], _KML.ANSI, 'Ukraine', None, 255),
    Region(
        'qa', 'xkb:qa::ara', 'Asia/Qatar', ['ar-QA', 'es'], _KML.ANSI,
        'Qatar', None, 256),
    Region(
        'mz', 'xkb:mz::por', 'Africa/Maputo', ['pt-MZ', 'vmw'],
        _KML.ANSI, 'Mozambique', None, 257)]
"""A list of :py:class:`regions.Region` objects for
**unconfirmed** regions. These may contain incorrect information (or not
supported by Chrome browser yet), and all fields must be reviewed before launch.
See http://goto/vpdsettings.

Currently, non-Latin keyboards must use an underlying Latin keyboard
for VPD. (This assumption should be revisited when moving items to
:py:data:`regions.Region.REGIONS_LIST`.)  This is
currently being discussed on <http://crbug.com/325389>.

Some timezones or locales may be missing from ``timezone_settings.cc`` (see
http://crosbug.com/p/23902).  This must be rectified before moving
items to :py:data:`regions.Region.REGIONS_LIST`.
"""


def ConsolidateRegions(regions):
  """Consolidates a list of regions into a dict.

  Args:
    regions: A list of Region objects.  All objects for any given
      region code must be identical or we will throw an exception.
      (We allow duplicates in case identical region objects are
      defined in both regions.py and the overlay, e.g., when moving
      items to the public overlay.)

  Returns:
    A dict from region code to Region.

  Raises:
    RegionException: If there are multiple regions defined for a given
      region, and the values for those regions differ.
  """
  # Build a dict from region_code to the first Region with that code.
  region_dict = {}
  for r in regions:
    existing_region = region_dict.get(r.region_code)
    if existing_region:
      if existing_region.GetFieldsDict() != r.GetFieldsDict():
        raise RegionException(
            'Conflicting definitions for region %r: %r, %r' %
            (r.region_code, existing_region.GetFieldsDict(),
             r.GetFieldsDict()))
    else:
      region_dict[r.region_code] = r

  return region_dict


def BuildRegionsDict(include_all=False):
  """Builds a dictionary mapping from code to :py:class:`regions.Region` object.

  The regions include:

  * :py:data:`regions.REGIONS_LIST`
  * :py:data:`regions_overlay.REGIONS_LIST`
  * Only if ``include_all`` is true:

    * :py:data:`regions.UNCONFIRMED_REGIONS_LIST`
    * :py:data:`regions.INCOMPLETE_REGIONS_LIST`

  A region may only appear in one of the above lists, or this function
  will (deliberately) fail.
  """
  regions = list(REGIONS_LIST)
  if include_all:
    known_codes = [r.region_code for r in regions]
    regions += [r for r in UNCONFIRMED_REGIONS_LIST if r.region_code not in
                known_codes]

  # Build dictionary of region code to list of regions with that
  # region code.  Check manually for duplicates, since the region may
  # be present both in the overlay and the public repo.
  return ConsolidateRegions(regions)


REGIONS = BuildRegionsDict()


def main(args=sys.argv[1:], out=None):
  parser = argparse.ArgumentParser(description=(
      'Display all known regions and their parameters. '))
  parser.add_argument('--format',
                      choices=('human-readable', 'csv', 'json', 'yaml'),
                      default='human-readable',
                      help='Output format (default=%(default)s)')
  parser.add_argument('--all', action='store_true',
                      help='Include unconfirmed and incomplete regions')
  parser.add_argument('--output', default=None,
                      help='Specify output file')
  parser.add_argument('--overlay', default=None,
                      help='Specify a Python file to overlay extra data')
  args = parser.parse_args(args)

  if args.overlay is not None:
    execfile(args.overlay)

  if args.all:
    # Add an additional 'confirmed' property to help identifying region status,
    # for autotests, unit tests and factory module.
    Region.FIELDS.insert(1, 'confirmed')
    for r in REGIONS_LIST:
      r.confirmed = True
    for r in UNCONFIRMED_REGIONS_LIST:
      r.confirmed = False

  regions_dict = BuildRegionsDict(args.all)

  if out is None:
    if args.output is None:
      out = sys.stdout
    else:
      out = open(args.output, 'w')

  # Handle YAML and JSON output.
  if args.format == 'yaml' or args.format == 'json':
    data = {}
    for region in regions_dict.values():
      item = {}
      for field in Region.FIELDS:
        item[field] = getattr(region, field)
      data[region.region_code] = item
    if args.format == 'yaml':
      yaml.dump(data, out)
    else:
      json.dump(data, out)
    return

  # Handle CSV or plain-text output: build a list of lines to print.
  # The CSV format is for publishing discussion spreadsheet so 'notes' should be
  # added.
  if args.format == 'csv':
    Region.FIELDS += ['notes']
  lines = [Region.FIELDS]

  def CoerceToString(value):
    """Returns the arguments in simple string type.

    If value is a list, concatenate its values with commas.  Otherwise, just
    return value.
    """
    if isinstance(value, list):
      return ','.join(value)
    else:
      return str(value)

  for region in sorted(regions_dict.values(), key=lambda v: v.region_code):
    lines.append([CoerceToString(getattr(region, field))
                  for field in Region.FIELDS])

  if args.format == 'csv':
    # Just print the lines in CSV format. Note the values may include ',' so the
    # separator must be tab.
    for l in lines:
      print('\t'.join(l))
  elif args.format == 'human-readable':
    num_columns = len(lines[0])

    # Calculate maximum length of each column.
    max_lengths = []
    for column_no in xrange(num_columns):
      max_lengths.append(max(len(line[column_no]) for line in lines))

    # Print each line, padding as necessary to the max column length.
    for line in lines:
      for column_no in xrange(num_columns):
        out.write(line[column_no].ljust(max_lengths[column_no] + 2))
      out.write('\n')
  else:
    exit('Sorry, unknown format specified: %s' % args.format)


if __name__ == '__main__':
  main()
