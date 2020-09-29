// Copyright 2018 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "shill/metrics.h"

#include <memory>
#include <utility>

#include <base/macros.h>
#include <base/stl_util.h>
#include <base/strings/string_util.h>
#include <base/strings/stringprintf.h>
#include <base/time/time.h>
#include <chromeos/dbus/service_constants.h>
#include <metrics/bootstat.h>

#include "shill/active_link_monitor.h"
#include "shill/connection_diagnostics.h"
#include "shill/link_monitor.h"
#include "shill/logging.h"

using std::string;

namespace shill {

namespace Logging {
static auto kModuleLogScope = ScopeLogger::kMetrics;
static string ObjectID(const Metrics* m) {
  return "(metrics)";
}
}  // namespace Logging

namespace {

constexpr char kMetricPrefix[] = "Network.Shill";

constexpr char kMetricsDailyChosenTechnologyAny[] = "daily.chosentech.any";
constexpr char kMetricsDailyChosenTechnologyWifi[] = "daily.chosentech.wifi";
constexpr char kMetricsDailyChosenTechnologyCellular[] =
    "daily.chosentech.cellular";

constexpr char kMetricsDailyChosenTimeOnlineAny[] =
    "Network.Shill.DailyChosenTimeOnline.Any";
constexpr char kMetricsDailyChosenTimeOnlineCellular[] =
    "Network.Shill.DailyChosenTimeOnline.Cellular";
constexpr char kMetricsDailyChosenTimeOnlineWifi[] =
    "Network.Shill.DailyChosenTimeOnline.Wifi";
constexpr char kMetricsDailyChosenFractionOnlineCellular[] =
    "Network.Shill.DailyChosenFractionOnline.Cellular";
constexpr char kMetricsDailyChosenFractionOnlineWifi[] =
    "Network.Shill.DailyChosenFractionOnline.Wifi";
constexpr base::TimeDelta kMetricsDailyTimeOnlineSamplePeriod =
    base::TimeDelta::FromMinutes(5);
constexpr base::TimeDelta kMetricsDailyTimeOnlineAccumulationPeriod =
    base::TimeDelta::FromDays(1);

constexpr char kMetricsMonthlyChosenTechnologyAny[] = "monthly.chosentech.any";
constexpr char kMetricsMonthlyChosenTechnologyWifi[] =
    "monthly.chosentech.wifi";
constexpr char kMetricsMonthlyChosenTechnologyCellular[] =
    "monthly.chosentech.cellular";

constexpr char kMetricsMonthlyChosenTimeOnlineAny[] =
    "Network.Shill.MonthlyChosenTimeOnline.Any";
constexpr char kMetricsMonthlyChosenTimeOnlineCellular[] =
    "Network.Shill.MonthlyChosenTimeOnline.Cellular";
constexpr char kMetricsMonthlyChosenTimeOnlineWifi[] =
    "Network.Shill.MonthlyChosenTimeOnline.Wifi";
constexpr char kMetricsMonthlyChosenFractionOnlineCellular[] =
    "Network.Shill.MonthlyChosenFractionOnline.Cellular";
constexpr char kMetricsMonthlyChosenFractionOnlineWifi[] =
    "Network.Shill.MonthlyChosenFractionOnline.Wifi";
constexpr base::TimeDelta kMetricsMonthlyTimeOnlineSamplePeriod =
    base::TimeDelta::FromMinutes(5);
constexpr base::TimeDelta kMetricsMonthlyTimeOnlineAccumulationPeriod =
    base::TimeDelta::FromDays(30);

}  // namespace

// static
// Our disconnect enumeration values are 0 (System Disconnect) and
// 1 (User Disconnect), see histograms.xml, but Chrome needs a minimum
// enum value of 1 and the minimum number of buckets needs to be 3 (see
// histogram.h).  Instead of remapping System Disconnect to 1 and
// User Disconnect to 2, we can just leave the enumerated values as-is
// because Chrome implicitly creates a [0-1) bucket for us.  Using Min=1,
// Max=2 and NumBuckets=3 gives us the following three buckets:
// [0-1), [1-2), [2-INT_MAX).  We end up with an extra bucket [2-INT_MAX)
// that we can safely ignore.
const char Metrics::kMetricDisconnectSuffix[] = "Disconnect";
const int Metrics::kMetricDisconnectMax = 2;
const int Metrics::kMetricDisconnectMin = 1;
const int Metrics::kMetricDisconnectNumBuckets = 3;

const char Metrics::kMetricSignalAtDisconnectSuffix[] = "SignalAtDisconnect";
const int Metrics::kMetricSignalAtDisconnectMin = 1;
const int Metrics::kMetricSignalAtDisconnectMax = 200;
const int Metrics::kMetricSignalAtDisconnectNumBuckets = 40;

const char Metrics::kMetricNetworkChannelSuffix[] = "Channel";
const int Metrics::kMetricNetworkChannelMax = Metrics::kWiFiChannelMax;
const char Metrics::kMetricNetworkEapInnerProtocolSuffix[] = "EapInnerProtocol";
const int Metrics::kMetricNetworkEapInnerProtocolMax =
    Metrics::kEapInnerProtocolMax;
const char Metrics::kMetricNetworkEapOuterProtocolSuffix[] = "EapOuterProtocol";
const int Metrics::kMetricNetworkEapOuterProtocolMax =
    Metrics::kEapOuterProtocolMax;
const char Metrics::kMetricNetworkPhyModeSuffix[] = "PhyMode";
const int Metrics::kMetricNetworkPhyModeMax = Metrics::kWiFiNetworkPhyModeMax;
const char Metrics::kMetricNetworkSecuritySuffix[] = "Security";
const int Metrics::kMetricNetworkSecurityMax = Metrics::kWiFiSecurityMax;
const char Metrics::kMetricNetworkServiceErrorSuffix[] = "ServiceErrors";
const char Metrics::kMetricNetworkServiceErrors[] =
    "Network.Shill.ServiceErrors";
const char Metrics::kMetricNetworkSignalStrengthSuffix[] = "SignalStrength";
const int Metrics::kMetricNetworkSignalStrengthMax = 200;
const int Metrics::kMetricNetworkSignalStrengthMin = 1;
const int Metrics::kMetricNetworkSignalStrengthNumBuckets = 40;

constexpr char
    Metrics::kMetricRememberedSystemWiFiNetworkCountBySecurityModeFormat[];
constexpr char
    Metrics::kMetricRememberedUserWiFiNetworkCountBySecurityModeFormat[];

const char Metrics::kMetricRememberedWiFiNetworkCount[] =
    "Network.Shill.WiFi.RememberedNetworkCount";
const int Metrics::kMetricRememberedWiFiNetworkCountMax = 1024;
const int Metrics::kMetricRememberedWiFiNetworkCountMin = 1;
const int Metrics::kMetricRememberedWiFiNetworkCountNumBuckets = 32;

const char Metrics::kMetricHiddenSSIDNetworkCount[] =
    "Network.Shill.WiFi.HiddenSSIDNetworkCount";

const char Metrics::kMetricTimeOnlineSecondsSuffix[] = "TimeOnline";
const int Metrics::kMetricTimeOnlineSecondsMax = 8 * 60 * 60;  // 8 hours
const int Metrics::kMetricTimeOnlineSecondsMin = 1;

const char Metrics::kMetricTimeToConnectMillisecondsSuffix[] = "TimeToConnect";
const int Metrics::kMetricTimeToConnectMillisecondsMax =
    60 * 1000;  // 60 seconds
const int Metrics::kMetricTimeToConnectMillisecondsMin = 1;
const int Metrics::kMetricTimeToConnectMillisecondsNumBuckets = 60;

const char Metrics::kMetricTimeToScanAndConnectMillisecondsSuffix[] =
    "TimeToScanAndConnect";

constexpr char Metrics::kMetricsCumulativeDirectory[] =
    "/var/lib/shill/metrics";
constexpr int Metrics::kMetricsCumulativeTimeOnlineBucketCount = 40;

const char Metrics::kMetricTimeToDropSeconds[] = "Network.Shill.TimeToDrop";
const int Metrics::kMetricTimeToDropSecondsMax = 8 * 60 * 60;  // 8 hours
const int Metrics::kMetricTimeToDropSecondsMin = 1;

const char Metrics::kMetricTimeToDisableMillisecondsSuffix[] = "TimeToDisable";
const int Metrics::kMetricTimeToDisableMillisecondsMax =
    60 * 1000;  // 60 seconds
const int Metrics::kMetricTimeToDisableMillisecondsMin = 1;
const int Metrics::kMetricTimeToDisableMillisecondsNumBuckets = 60;

const char Metrics::kMetricTimeToEnableMillisecondsSuffix[] = "TimeToEnable";
const int Metrics::kMetricTimeToEnableMillisecondsMax =
    60 * 1000;  // 60 seconds
const int Metrics::kMetricTimeToEnableMillisecondsMin = 1;
const int Metrics::kMetricTimeToEnableMillisecondsNumBuckets = 60;

const char Metrics::kMetricTimeToInitializeMillisecondsSuffix[] =
    "TimeToInitialize";
const int Metrics::kMetricTimeToInitializeMillisecondsMax =
    30 * 1000;  // 30 seconds
const int Metrics::kMetricTimeToInitializeMillisecondsMin = 1;
const int Metrics::kMetricTimeToInitializeMillisecondsNumBuckets = 30;

const char Metrics::kMetricTimeResumeToReadyMillisecondsSuffix[] =
    "TimeResumeToReady";
const char Metrics::kMetricTimeToConfigMillisecondsSuffix[] = "TimeToConfig";
const char Metrics::kMetricTimeToJoinMillisecondsSuffix[] = "TimeToJoin";
const char Metrics::kMetricTimeToOnlineMillisecondsSuffix[] = "TimeToOnline";
const char Metrics::kMetricTimeToPortalMillisecondsSuffix[] = "TimeToPortal";
const char Metrics::kMetricTimeToRedirectFoundMillisecondsSuffix[] =
    "TimeToRedirectFound";

const char Metrics::kMetricTimeToScanMillisecondsSuffix[] = "TimeToScan";
const int Metrics::kMetricTimeToScanMillisecondsMax = 180 * 1000;  // 3 minutes
const int Metrics::kMetricTimeToScanMillisecondsMin = 1;
const int Metrics::kMetricTimeToScanMillisecondsNumBuckets = 90;

const int Metrics::kTimerHistogramMillisecondsMax = 45 * 1000;
const int Metrics::kTimerHistogramMillisecondsMin = 1;
const int Metrics::kTimerHistogramNumBuckets = 50;

const char Metrics::kMetricPortalAttemptsToOnlineSuffix[] =
    "PortalAttemptsToOnline";
const int Metrics::kMetricPortalAttemptsToOnlineMax = 100;
const int Metrics::kMetricPortalAttemptsToOnlineMin = 1;
const int Metrics::kMetricPortalAttemptsToOnlineNumBuckets = 10;

const char Metrics::kMetricPortalResultSuffix[] = "PortalResult";

const char Metrics::kMetricScanResult[] = "Network.Shill.WiFi.ScanResult";
const char Metrics::kMetricWiFiScanTimeInEbusyMilliseconds[] =
    "Network.Shill.WiFi.ScanTimeInEbusy";

const char Metrics::kMetricTerminationActionTimeTaken[] =
    "Network.Shill.TerminationActionTimeTaken";
const char Metrics::kMetricTerminationActionResult[] =
    "Network.Shill.TerminationActionResult";
const int Metrics::kMetricTerminationActionTimeTakenMillisecondsMax = 20000;
const int Metrics::kMetricTerminationActionTimeTakenMillisecondsMin = 1;

const char Metrics::kMetricSuspendActionTimeTaken[] =
    "Network.Shill.SuspendActionTimeTaken";
const char Metrics::kMetricSuspendActionResult[] =
    "Network.Shill.SuspendActionResult";
const int Metrics::kMetricSuspendActionTimeTakenMillisecondsMax = 20000;
const int Metrics::kMetricSuspendActionTimeTakenMillisecondsMin = 1;

const char Metrics::kMetricDarkResumeActionTimeTaken[] =
    "Network.Shill.DarkResumeActionTimeTaken";
const char Metrics::kMetricDarkResumeActionResult[] =
    "Network.Shill.DarkResumeActionResult";
const int Metrics::kMetricDarkResumeActionTimeTakenMillisecondsMax = 20000;
const int Metrics::kMetricDarkResumeActionTimeTakenMillisecondsMin = 1;
const char Metrics::kMetricDarkResumeUnmatchedScanResultReceived[] =
    "Network.Shill.WiFi.DarkResumeUnmatchedScanResultsReceived";

const char Metrics::kMetricWakeOnWiFiFeaturesEnabledState[] =
    "Network.Shill.WiFi.WakeOnWiFiFeaturesEnabledState";
const char Metrics::kMetricVerifyWakeOnWiFiSettingsResult[] =
    "Network.Shill.WiFi.VerifyWakeOnWiFiSettingsResult";
const char Metrics::kMetricWiFiConnectionStatusAfterWake[] =
    "Network.Shill.WiFi.WiFiConnectionStatusAfterWake";
const char Metrics::kMetricWakeOnWiFiThrottled[] =
    "Network.Shill.WiFi.WakeOnWiFiThrottled";
const char Metrics::kMetricWakeReasonReceivedBeforeOnDarkResume[] =
    "Network.Shill.WiFi.WakeReasonReceivedBeforeOnDarkResume";
const char Metrics::kMetricDarkResumeWakeReason[] =
    "Network.Shill.WiFi.DarkResumeWakeReason";
const char Metrics::kMetricDarkResumeScanType[] =
    "Network.Shill.WiFi.DarkResumeScanType";
const char Metrics::kMetricDarkResumeScanRetryResult[] =
    "Network.Shill.WiFi.DarkResumeScanRetryResult";
const char Metrics::kMetricDarkResumeScanNumRetries[] =
    "Network.Shill.WiFi.DarkResumeScanNumRetries";
const int Metrics::kMetricDarkResumeScanNumRetriesMax = 20;
const int Metrics::kMetricDarkResumeScanNumRetriesMin = 0;

const char Metrics::kMetricSuspendDurationWoWOnConnected[] =
    "Network.Shill.WiFi.SuspendDurationWoWOnConnected";
const char Metrics::kMetricSuspendDurationWoWOnDisconnected[] =
    "Network.Shill.WiFi.SuspendDurationWoWOnDisconnected";
const char Metrics::kMetricSuspendDurationWoWOffConnected[] =
    "Network.Shill.WiFi.SuspendDurationWoWOffConnected";
const char Metrics::kMetricSuspendDurationWoWOffDisconnected[] =
    "Network.Shill.WiFi.SuspendDurationWoWOffDisconnected";
const int Metrics::kSuspendDurationMin = 1;
// Max suspend duration that we care about, for the purpose
// of tracking wifi disconnect on resume. Set to 1 day.
const int Metrics::kSuspendDurationMax = 86400;
const int Metrics::kSuspendDurationNumBuckets = 60;

// static
const uint16_t Metrics::kWiFiBandwidth5MHz = 5;
const uint16_t Metrics::kWiFiBandwidth20MHz = 20;
const uint16_t Metrics::kWiFiFrequency2412 = 2412;
const uint16_t Metrics::kWiFiFrequency2472 = 2472;
const uint16_t Metrics::kWiFiFrequency2484 = 2484;
const uint16_t Metrics::kWiFiFrequency5170 = 5170;
const uint16_t Metrics::kWiFiFrequency5180 = 5180;
const uint16_t Metrics::kWiFiFrequency5230 = 5230;
const uint16_t Metrics::kWiFiFrequency5240 = 5240;
const uint16_t Metrics::kWiFiFrequency5320 = 5320;
const uint16_t Metrics::kWiFiFrequency5500 = 5500;
const uint16_t Metrics::kWiFiFrequency5700 = 5700;
const uint16_t Metrics::kWiFiFrequency5745 = 5745;
const uint16_t Metrics::kWiFiFrequency5825 = 5825;

// static
const char Metrics::kMetricPowerManagerKey[] = "metrics";

// static
const char Metrics::kMetricLinkMonitorFailureSuffix[] = "LinkMonitorFailure";
const char Metrics::kMetricLinkMonitorResponseTimeSampleSuffix[] =
    "LinkMonitorResponseTimeSample";
const int Metrics::kMetricLinkMonitorResponseTimeSampleMin = 1;
const int Metrics::kMetricLinkMonitorResponseTimeSampleMax =
    ActiveLinkMonitor::kDefaultTestPeriodMilliseconds;
const int Metrics::kMetricLinkMonitorResponseTimeSampleNumBuckets = 50;
const char Metrics::kMetricLinkMonitorSecondsToFailureSuffix[] =
    "LinkMonitorSecondsToFailure";
const int Metrics::kMetricLinkMonitorSecondsToFailureMin = 1;
const int Metrics::kMetricLinkMonitorSecondsToFailureMax = 7200;
const int Metrics::kMetricLinkMonitorSecondsToFailureNumBuckets = 50;
const char Metrics::kMetricLinkMonitorBroadcastErrorsAtFailureSuffix[] =
    "LinkMonitorBroadcastErrorsAtFailure";
const char Metrics::kMetricLinkMonitorUnicastErrorsAtFailureSuffix[] =
    "LinkMonitorUnicastErrorsAtFailure";
const int Metrics::kMetricLinkMonitorErrorCountMin = 1;
const int Metrics::kMetricLinkMonitorErrorCountMax =
    LinkMonitor::kFailureThreshold;
const int Metrics::kMetricLinkMonitorErrorCountNumBuckets =
    LinkMonitor::kFailureThreshold + 1;

// static
const char Metrics::kMetricNeighborLinkMonitorFailureSuffix[] =
    "NeighborLinkMonitorFailure";

// static
const char Metrics::kMetricApChannelSwitch[] =
    "Network.Shill.WiFi.ApChannelSwitch";

// static
const char Metrics::kMetricAp80211kSupport[] =
    "Network.Shill.WiFi.Ap80211kSupport";
const char Metrics::kMetricAp80211rSupport[] =
    "Network.Shill.WiFi.Ap80211rSupport";
const char Metrics::kMetricAp80211vDMSSupport[] =
    "Network.Shill.WiFi.Ap80211vDMSSupport";
const char Metrics::kMetricAp80211vBSSMaxIdlePeriodSupport[] =
    "Network.Shill.WiFi.Ap80211vBSSMaxIdlePeriodSupport";
const char Metrics::kMetricAp80211vBSSTransitionSupport[] =
    "Network.Shill.WiFi.Ap80211vBSSTransitionSupport";

// static
const char Metrics::kMetricLinkClientDisconnectReason[] =
    "Network.Shill.WiFi.ClientDisconnectReason";
const char Metrics::kMetricLinkApDisconnectReason[] =
    "Network.Shill.WiFi.ApDisconnectReason";
const char Metrics::kMetricLinkClientDisconnectType[] =
    "Network.Shill.WiFi.ClientDisconnectType";
const char Metrics::kMetricLinkApDisconnectType[] =
    "Network.Shill.WiFi.ApDisconnectType";
const char Metrics::kMetricWiFiAssocFailureType[] =
    "Network.Shill.WiFi.AssocFailureType";
const char Metrics::kMetricWiFiAuthFailureType[] =
    "Network.Shill.WiFi.AuthFailureType";

// static
const char Metrics::kMetricWifiRoamTimePrefix[] = "Network.Shill.WiFi.RoamTime";
const int Metrics::kMetricWifiRoamTimeMillisecondsMax = 1000;
const int Metrics::kMetricWifiRoamTimeMillisecondsMin = 1;
const int Metrics::kMetricWifiRoamTimeNumBuckets = 20;

// static
const char Metrics::kMetricWifiRoamCompletePrefix[] =
    "Network.Shill.WiFi.RoamComplete";

// static
const char Metrics::kMetricWifiSessionLengthPrefix[] =
    "Network.Shill.WiFi.SessionLength";
const int Metrics::kMetricWifiSessionLengthMillisecondsMax = 10000;
const int Metrics::kMetricWifiSessionLengthMillisecondsMin = 1;
const int Metrics::kMetricWifiSessionLengthNumBuckets = 20;

const char Metrics::kMetricWifiPSKSuffix[] = "PSK";
const char Metrics::kMetricWifiFTPSKSuffix[] = "FTPSK";
const char Metrics::kMetricWifiEAPSuffix[] = "EAP";
const char Metrics::kMetricWifiFTEAPSuffix[] = "FTEAP";

// static
const char Metrics::kMetricCellular3GPPRegistrationDelayedDrop[] =
    "Network.Shill.Cellular.3GPPRegistrationDelayedDrop";
const char Metrics::kMetricCellularAutoConnectTries[] =
    "Network.Shill.Cellular.AutoConnectTries";
const int Metrics::kMetricCellularAutoConnectTriesMax = 20;
const int Metrics::kMetricCellularAutoConnectTriesMin = 1;
const int Metrics::kMetricCellularAutoConnectTriesNumBuckets = 20;
const char Metrics::kMetricCellularAutoConnectTotalTime[] =
    "Network.Shill.Cellular.AutoConnectTotalTime";
const int Metrics::kMetricCellularAutoConnectTotalTimeMax =
    60 * 1000;  // 60 seconds
const int Metrics::kMetricCellularAutoConnectTotalTimeMin = 1;
const int Metrics::kMetricCellularAutoConnectTotalTimeNumBuckets = 60;
const char Metrics::kMetricCellularDrop[] = "Network.Shill.Cellular.Drop";

// static
const char Metrics::kMetricCellularFailure[] = "Network.Shill.Cellular.Failure";
const int Metrics::kMetricCellularConnectionFailure = 0;
const int Metrics::kMetricCellularDisconnectionFailure = 1;
const int Metrics::kMetricCellularMaxFailure =
    kMetricCellularDisconnectionFailure + 1;

const char Metrics::kMetricCellularOutOfCreditsReason[] =
    "Network.Shill.Cellular.OutOfCreditsReason";
const char Metrics::kMetricCellularSignalStrengthBeforeDrop[] =
    "Network.Shill.Cellular.SignalStrengthBeforeDrop";
const int Metrics::kMetricCellularSignalStrengthBeforeDropMax = 100;
const int Metrics::kMetricCellularSignalStrengthBeforeDropMin = 1;
const int Metrics::kMetricCellularSignalStrengthBeforeDropNumBuckets = 10;

// static
const char Metrics::kMetricCorruptedProfile[] =
    "Network.Shill.CorruptedProfile";

// static
const char Metrics::kMetricVpnDriver[] = "Network.Shill.Vpn.Driver";
const int Metrics::kMetricVpnDriverMax = Metrics::kVpnDriverMax;
const char Metrics::kMetricVpnRemoteAuthenticationType[] =
    "Network.Shill.Vpn.RemoteAuthenticationType";
const int Metrics::kMetricVpnRemoteAuthenticationTypeMax =
    Metrics::kVpnRemoteAuthenticationTypeMax;
const char Metrics::kMetricVpnUserAuthenticationType[] =
    "Network.Shill.Vpn.UserAuthenticationType";
const int Metrics::kMetricVpnUserAuthenticationTypeMax =
    Metrics::kVpnUserAuthenticationTypeMax;

// CL:557297 changed the number of buckets for the 'ExpiredLeaseLengthSeconds'
// metric. That would lead to confusing display of samples collected before and
// after the change. To avoid that, the 'ExpiredLeaseLengthSeconds' metric is
// renamed to 'ExpiredLeaseLengthSeconds2'.
const char Metrics::kMetricExpiredLeaseLengthSecondsSuffix[] =
    "ExpiredLeaseLengthSeconds2";
const int Metrics::kMetricExpiredLeaseLengthSecondsMax =
    7 * 24 * 60 * 60;  // 7 days
const int Metrics::kMetricExpiredLeaseLengthSecondsMin = 1;
const int Metrics::kMetricExpiredLeaseLengthSecondsNumBuckets = 100;

// static
const char Metrics::kMetricWifiAutoConnectableServices[] =
    "Network.Shill.WiFi.AutoConnectableServices";
const int Metrics::kMetricWifiAutoConnectableServicesMax = 50;
const int Metrics::kMetricWifiAutoConnectableServicesMin = 1;
const int Metrics::kMetricWifiAutoConnectableServicesNumBuckets = 10;

// static
const char Metrics::kMetricWifiAvailableBSSes[] =
    "Network.Shill.WiFi.AvailableBSSesAtConnect";
const int Metrics::kMetricWifiAvailableBSSesMax = 50;
const int Metrics::kMetricWifiAvailableBSSesMin = 1;
const int Metrics::kMetricWifiAvailableBSSesNumBuckets = 10;

// static
const char Metrics::kMetricWifiStoppedTxQueueReason[] =
    "Network.Shill.WiFi.StoppedTxQueueReason";
// Values are defined in mac80211_monitor.h.

// static
const char Metrics::kMetricWifiStoppedTxQueueLength[] =
    "Network.Shill.WiFi.StoppedTxQueueLength";
const int Metrics::kMetricWifiStoppedTxQueueLengthMax = 10000;
const int Metrics::kMetricWifiStoppedTxQueueLengthMin = 1;
const int Metrics::kMetricWifiStoppedTxQueueLengthNumBuckets = 50;

// Number of services associated with currently connected network.
const char Metrics::kMetricServicesOnSameNetwork[] =
    "Network.Shill.ServicesOnSameNetwork";
const int Metrics::kMetricServicesOnSameNetworkMax = 20;
const int Metrics::kMetricServicesOnSameNetworkMin = 1;
const int Metrics::kMetricServicesOnSameNetworkNumBuckets = 10;

// static
const char Metrics::kMetricUserInitiatedEvents[] =
    "Network.Shill.UserInitiatedEvents";

// static
const char Metrics::kMetricWifiTxBitrate[] =
    "Network.Shill.WiFi.TransmitBitrateMbps";
const int Metrics::kMetricWifiTxBitrateMax = 7000;
const int Metrics::kMetricWifiTxBitrateMin = 1;
const int Metrics::kMetricWifiTxBitrateNumBuckets = 100;

// static
const char Metrics::kMetricWifiUserInitiatedConnectionResult[] =
    "Network.Shill.WiFi.UserInitiatedConnectionResult";

// static
const char Metrics::kMetricWifiUserInitiatedConnectionFailureReason[] =
    "Network.Shill.WiFi.UserInitiatedConnectionFailureReason";

// static
const char Metrics::kMetricWifiSupplicantAttempts[] =
    "Network.Shill.WiFi.SupplicantAttempts";
const int Metrics::kMetricWifiSupplicantAttemptsMax = 10;
const int Metrics::kMetricWifiSupplicantAttemptsMin = 1;
const int Metrics::kMetricWifiSupplicantAttemptsNumBuckets = 11;

// static
const char Metrics::kMetricFallbackDNSTestResultSuffix[] =
    "FallbackDNSTestResult";

// static
const char Metrics::kMetricNetworkProblemDetectedSuffix[] =
    "NetworkProblemDetected";

// static
const char Metrics::kMetricDeviceConnectionStatus[] =
    "Network.Shill.DeviceConnectionStatus";

// static
const char Metrics::kMetricDhcpClientStatus[] =
    "Network.Shill.DHCPClientStatus";

// static
const char Metrics::kMetricDhcpClientMTUValue[] =
    "Network.Shill.DHCPClientMTUValue";
const char Metrics::kMetricPPPMTUValue[] = "Network.Shill.PPPMTUValue";

// static
const char Metrics::kMetricNetworkConnectionIPTypeSuffix[] =
    "NetworkConnectionIPType";

// static
const char Metrics::kMetricIPv6ConnectivityStatusSuffix[] =
    "IPv6ConnectivityStatus";

// static
const char Metrics::kMetricDevicePresenceStatusSuffix[] =
    "DevicePresenceStatus";

// static
const char Metrics::kMetricDeviceRemovedEvent[] =
    "Network.Shill.DeviceRemovedEvent";

// static
const char Metrics::kMetricConnectionDiagnosticsIssue[] =
    "Network.Shill.ConnectionDiagnosticsIssue";

// static
const char Metrics::kMetricPortalDetectionMultiProbeResult[] =
    "Network.Shill.PortalDetectionMultiProbeResult";

// static
const char Metrics::kMetricRegulatoryDomain[] =
    "Network.Shill.WiFi.RegulatoryDomain";

// static
const char Metrics::kMetricHS20Support[] = "Network.Shill.WiFi.HS20Support";

// static
const char Metrics::kMetricUnreliableLinkSignalStrengthSuffix[] =
    "UnreliableLinkSignalStrength";
const int Metrics::kMetricServiceSignalStrengthMin = 1;
const int Metrics::kMetricServiceSignalStrengthMax = 100;
const int Metrics::kMetricServiceSignalStrengthNumBuckets = 40;

Metrics::Metrics()
    : library_(&metrics_library_),
      last_default_technology_(Technology::kUnknown),
      last_physical_technology_(Technology::kUnknown),
      was_last_online_(false),
      time_online_timer_(new chromeos_metrics::Timer),
      time_to_drop_timer_(new chromeos_metrics::Timer),
      time_resume_to_ready_timer_(new chromeos_metrics::Timer),
      time_termination_actions_timer(new chromeos_metrics::Timer),
      time_suspend_actions_timer(new chromeos_metrics::Timer),
      time_dark_resume_actions_timer(new chromeos_metrics::Timer),
      collect_bootstats_(true),
      num_scan_results_expected_in_dark_resume_(0),
      wake_on_wifi_throttled_(false),
      wake_reason_received_(false),
      dark_resume_scan_retries_(0) {
  chromeos_metrics::TimerReporter::set_metrics_lib(library_);
}

Metrics::~Metrics() = default;

// static
Metrics::WiFiChannel Metrics::WiFiFrequencyToChannel(uint16_t frequency) {
  WiFiChannel channel = kWiFiChannelUndef;
  if (kWiFiFrequency2412 <= frequency && frequency <= kWiFiFrequency2472) {
    if (((frequency - kWiFiFrequency2412) % kWiFiBandwidth5MHz) == 0)
      channel = static_cast<WiFiChannel>(kWiFiChannel2412 +
                                         (frequency - kWiFiFrequency2412) /
                                             kWiFiBandwidth5MHz);
  } else if (frequency == kWiFiFrequency2484) {
    channel = kWiFiChannel2484;
  } else if (kWiFiFrequency5170 <= frequency &&
             frequency <= kWiFiFrequency5230) {
    if ((frequency % kWiFiBandwidth20MHz) == 0)
      channel = static_cast<WiFiChannel>(kWiFiChannel5180 +
                                         (frequency - kWiFiFrequency5180) /
                                             kWiFiBandwidth20MHz);
    if ((frequency % kWiFiBandwidth20MHz) == 10)
      channel = static_cast<WiFiChannel>(kWiFiChannel5170 +
                                         (frequency - kWiFiFrequency5170) /
                                             kWiFiBandwidth20MHz);
  } else if (kWiFiFrequency5240 <= frequency &&
             frequency <= kWiFiFrequency5320) {
    if (((frequency - kWiFiFrequency5180) % kWiFiBandwidth20MHz) == 0)
      channel = static_cast<WiFiChannel>(kWiFiChannel5180 +
                                         (frequency - kWiFiFrequency5180) /
                                             kWiFiBandwidth20MHz);
  } else if (kWiFiFrequency5500 <= frequency &&
             frequency <= kWiFiFrequency5700) {
    if (((frequency - kWiFiFrequency5500) % kWiFiBandwidth20MHz) == 0)
      channel = static_cast<WiFiChannel>(kWiFiChannel5500 +
                                         (frequency - kWiFiFrequency5500) /
                                             kWiFiBandwidth20MHz);
  } else if (kWiFiFrequency5745 <= frequency &&
             frequency <= kWiFiFrequency5825) {
    if (((frequency - kWiFiFrequency5745) % kWiFiBandwidth20MHz) == 0)
      channel = static_cast<WiFiChannel>(kWiFiChannel5745 +
                                         (frequency - kWiFiFrequency5745) /
                                             kWiFiBandwidth20MHz);
  }
  CHECK(kWiFiChannelUndef <= channel && channel < kWiFiChannelMax);

  if (channel == kWiFiChannelUndef)
    LOG(WARNING) << "no mapping for frequency " << frequency;
  else
    SLOG(nullptr, 3) << "mapped frequency " << frequency << " to enum bucket "
                     << channel;

  return channel;
}

// static
Metrics::WiFiFrequencyRange Metrics::WiFiChannelToFrequencyRange(
    Metrics::WiFiChannel channel) {
  if (channel >= kWiFiChannelMin24 && channel <= kWiFiChannelMax24) {
    return kWiFiFrequencyRange24;
  } else if (channel >= kWiFiChannelMin5 && channel <= kWiFiChannelMax5) {
    return kWiFiFrequencyRange5;
  } else {
    return kWiFiFrequencyRangeUndef;
  }
}

// static
Metrics::WiFiSecurity Metrics::WiFiSecurityStringToEnum(
    const string& security) {
  if (security == kSecurityNone) {
    return kWiFiSecurityNone;
  } else if (security == kSecurityWep) {
    return kWiFiSecurityWep;
  } else if (security == kSecurityWpa) {
    return kWiFiSecurityWpa;
  } else if (security == kSecurityRsn) {
    return kWiFiSecurityRsn;
  } else if (security == kSecurity8021x) {
    return kWiFiSecurity8021x;
  } else if (security == kSecurityPsk) {
    return kWiFiSecurityPsk;
  } else if (security == kSecurityWpa3) {
    return kWiFiSecurityWpa3;
  } else {
    return kWiFiSecurityUnknown;
  }
}

// static
Metrics::EapOuterProtocol Metrics::EapOuterProtocolStringToEnum(
    const string& outer) {
  if (outer == kEapMethodPEAP) {
    return kEapOuterProtocolPeap;
  } else if (outer == kEapMethodTLS) {
    return kEapOuterProtocolTls;
  } else if (outer == kEapMethodTTLS) {
    return kEapOuterProtocolTtls;
  } else if (outer == kEapMethodLEAP) {
    return kEapOuterProtocolLeap;
  } else {
    return kEapOuterProtocolUnknown;
  }
}

// static
Metrics::EapInnerProtocol Metrics::EapInnerProtocolStringToEnum(
    const string& inner) {
  if (inner.empty()) {
    return kEapInnerProtocolNone;
  } else if (inner == kEapPhase2AuthPEAPMD5) {
    return kEapInnerProtocolPeapMd5;
  } else if (inner == kEapPhase2AuthPEAPMSCHAPV2) {
    return kEapInnerProtocolPeapMschapv2;
  } else if (inner == kEapPhase2AuthTTLSEAPMD5) {
    return kEapInnerProtocolTtlsEapMd5;
  } else if (inner == kEapPhase2AuthTTLSEAPMSCHAPV2) {
    return kEapInnerProtocolTtlsEapMschapv2;
  } else if (inner == kEapPhase2AuthTTLSMSCHAPV2) {
    return kEapInnerProtocolTtlsMschapv2;
  } else if (inner == kEapPhase2AuthTTLSMSCHAP) {
    return kEapInnerProtocolTtlsMschap;
  } else if (inner == kEapPhase2AuthTTLSPAP) {
    return kEapInnerProtocolTtlsPap;
  } else if (inner == kEapPhase2AuthTTLSCHAP) {
    return kEapInnerProtocolTtlsChap;
  } else {
    return kEapInnerProtocolUnknown;
  }
}

// static
Metrics::PortalResult Metrics::PortalDetectionResultToEnum(
    const PortalDetector::Result& portal_result) {
  PortalResult retval = kPortalResultUnknown;
  // The only time we should end a successful portal detection is when we're
  // in the Content phase.  If we end with kStatusSuccess in any other phase,
  // then this indicates that something bad has happened.
  switch (portal_result.phase) {
    case PortalDetector::Phase::kDNS:
      if (portal_result.status == PortalDetector::Status::kFailure)
        retval = kPortalResultDNSFailure;
      else if (portal_result.status == PortalDetector::Status::kTimeout)
        retval = kPortalResultDNSTimeout;
      else
        LOG(DFATAL) << __func__ << ": Final result status "
                    << static_cast<int>(portal_result.status)
                    << " is not allowed in the DNS phase";
      break;

    case PortalDetector::Phase::kConnection:
      if (portal_result.status == PortalDetector::Status::kFailure)
        retval = kPortalResultConnectionFailure;
      else if (portal_result.status == PortalDetector::Status::kTimeout)
        retval = kPortalResultConnectionTimeout;
      else
        LOG(DFATAL) << __func__ << ": Final result status "
                    << static_cast<int>(portal_result.status)
                    << " is not allowed in the Connection phase";
      break;

    case PortalDetector::Phase::kHTTP:
      if (portal_result.status == PortalDetector::Status::kFailure)
        retval = kPortalResultHTTPFailure;
      else if (portal_result.status == PortalDetector::Status::kTimeout)
        retval = kPortalResultHTTPTimeout;
      else
        LOG(DFATAL) << __func__ << ": Final result status "
                    << static_cast<int>(portal_result.status)
                    << " is not allowed in the HTTP phase";
      break;

    case PortalDetector::Phase::kContent:
      if (portal_result.status == PortalDetector::Status::kSuccess)
        retval = kPortalResultSuccess;
      else if (portal_result.status == PortalDetector::Status::kFailure)
        retval = kPortalResultContentFailure;
      else if (portal_result.status == PortalDetector::Status::kRedirect)
        retval = kPortalResultContentRedirect;
      else if (portal_result.status == PortalDetector::Status::kTimeout)
        retval = kPortalResultContentTimeout;
      else
        LOG(DFATAL) << __func__ << ": Final result status "
                    << static_cast<int>(portal_result.status)
                    << " is not allowed in the Content phase";
      break;

    case PortalDetector::Phase::kUnknown:
      retval = kPortalResultUnknown;
      break;

    default:
      LOG(DFATAL) << __func__ << ": Invalid phase "
                  << static_cast<int>(portal_result.phase);
      break;
  }

  return retval;
}

void Metrics::Start() {
  SLOG(this, 2) << __func__;
  static const char* const daily_cumulative_names_array[] = {
      [CHOSEN_ANY] = kMetricsDailyChosenTechnologyAny,
      [CHOSEN_CELLULAR] = kMetricsDailyChosenTechnologyCellular,
      [CHOSEN_WIFI] = kMetricsDailyChosenTechnologyWifi,
  };
  static const char* const monthly_cumulative_names_array[] = {
      [CHOSEN_ANY] = kMetricsMonthlyChosenTechnologyAny,
      [CHOSEN_CELLULAR] = kMetricsMonthlyChosenTechnologyCellular,
      [CHOSEN_WIFI] = kMetricsMonthlyChosenTechnologyWifi,
  };
  static const char* const daily_histogram_names_array[] = {
      [CHOSEN_ANY] = kMetricsDailyChosenTimeOnlineAny,
      [CHOSEN_CELLULAR] = kMetricsDailyChosenTimeOnlineCellular,
      [CHOSEN_WIFI] = kMetricsDailyChosenTimeOnlineWifi,
      [CHOSEN_FRACTION_CELLULAR] = kMetricsDailyChosenFractionOnlineCellular,
      [CHOSEN_FRACTION_WIFI] = kMetricsDailyChosenFractionOnlineWifi,
  };
  static const char* const monthly_histogram_names_array[] = {
      [CHOSEN_ANY] = kMetricsMonthlyChosenTimeOnlineAny,
      [CHOSEN_CELLULAR] = kMetricsMonthlyChosenTimeOnlineCellular,
      [CHOSEN_WIFI] = kMetricsMonthlyChosenTimeOnlineWifi,
      [CHOSEN_FRACTION_CELLULAR] = kMetricsMonthlyChosenFractionOnlineCellular,
      [CHOSEN_FRACTION_WIFI] = kMetricsMonthlyChosenFractionOnlineWifi,
  };
  static const std::vector<std::string> daily_cumulative_names(
      daily_cumulative_names_array,
      daily_cumulative_names_array + base::size(daily_cumulative_names_array));
  static const std::vector<std::string> monthly_cumulative_names(
      monthly_cumulative_names_array,
      monthly_cumulative_names_array +
          base::size(monthly_cumulative_names_array));
  static const std::vector<std::string> daily_histogram_names(
      daily_histogram_names_array,
      daily_histogram_names_array + base::size(daily_histogram_names_array));
  static const std::vector<std::string> monthly_histogram_names(
      monthly_histogram_names_array,
      monthly_histogram_names_array +
          base::size(monthly_histogram_names_array));

  base::FilePath backing_path(kMetricsCumulativeDirectory);

  daily_metrics_.reset(new chromeos_metrics::CumulativeMetrics(
      backing_path, daily_cumulative_names, kMetricsDailyTimeOnlineSamplePeriod,
      base::Bind(&AccumulateTimeOnTechnology, this, daily_cumulative_names),
      kMetricsDailyTimeOnlineAccumulationPeriod,
      base::Bind(
          &ReportTimeOnTechnology, base::Unretained(&metrics_library_),
          daily_histogram_names, 10 /* histogram min (seconds) */,
          kMetricsDailyTimeOnlineAccumulationPeriod.InSeconds() /* max */,
          daily_cumulative_names)));
  monthly_metrics_.reset(new chromeos_metrics::CumulativeMetrics(
      backing_path, monthly_cumulative_names,
      kMetricsMonthlyTimeOnlineSamplePeriod,
      base::Bind(&AccumulateTimeOnTechnology, this, monthly_cumulative_names),
      kMetricsMonthlyTimeOnlineAccumulationPeriod,
      base::Bind(
          &ReportTimeOnTechnology, base::Unretained(&metrics_library_),
          monthly_histogram_names, 10 /* histogram min (seconds) */,
          kMetricsMonthlyTimeOnlineAccumulationPeriod.InSeconds() /* max */,
          monthly_cumulative_names)));
}

void Metrics::Stop() {
  SLOG(this, 2) << __func__;
}

void Metrics::RegisterService(const Service& service) {
  SLOG(this, 2) << __func__;
  LOG_IF(WARNING, base::Contains(services_metrics_, &service))
      << "Repeatedly registering " << service.log_name();
  services_metrics_[&service] = std::make_unique<ServiceMetrics>();
  InitializeCommonServiceMetrics(service);
}

void Metrics::DeregisterService(const Service& service) {
  services_metrics_.erase(&service);
}

void Metrics::AddServiceStateTransitionTimer(const Service& service,
                                             const string& histogram_name,
                                             Service::ConnectState start_state,
                                             Service::ConnectState stop_state) {
  SLOG(this, 2) << __func__ << ": adding " << histogram_name << " for "
                << Service::ConnectStateToString(start_state) << " -> "
                << Service::ConnectStateToString(stop_state);
  ServiceMetricsLookupMap::iterator it = services_metrics_.find(&service);
  if (it == services_metrics_.end()) {
    SLOG(this, 1) << "service not found";
    DCHECK(false);
    return;
  }
  ServiceMetrics* service_metrics = it->second.get();
  CHECK(start_state < stop_state);
  auto timer = std::make_unique<chromeos_metrics::TimerReporter>(
      histogram_name, kTimerHistogramMillisecondsMin,
      kTimerHistogramMillisecondsMax, kTimerHistogramNumBuckets);
  service_metrics->start_on_state[start_state].push_back(timer.get());
  service_metrics->stop_on_state[stop_state].push_back(timer.get());
  service_metrics->timers.push_back(std::move(timer));
}

// static
void Metrics::AccumulateTimeOnTechnology(
    const Metrics* metrics,
    const std::vector<std::string> cumulative_names,
    chromeos_metrics::CumulativeMetrics* cm) {
  // Only accumulate time on line.
  if (!metrics->was_last_online_)
    return;

  int64_t active_time = cm->ActiveTimeSinceLastUpdate().InSeconds();
  bool wifi_is_available = metrics->IsTechnologyPresent(Technology::kWifi);
  bool cellular_is_available =
      metrics->IsTechnologyPresent(Technology::kCellular);

  if (wifi_is_available && cellular_is_available) {
    cm->Add(cumulative_names[CHOSEN_ANY], active_time);
  }

  switch (metrics->last_default_technology_) {
    case Technology::kCellular:
      if (wifi_is_available) {
        cm->Add(cumulative_names[CHOSEN_CELLULAR], active_time);
      }
      break;
    case Technology::kWifi:
      if (cellular_is_available) {
        cm->Add(cumulative_names[CHOSEN_WIFI], active_time);
      }
      break;
    default:
      break;
  }
}

// static
void Metrics::ReportTimeOnTechnology(
    MetricsLibraryInterface* mli,
    const std::vector<std::string> histogram_names,
    const int min_seconds,
    const int max_seconds,
    const std::vector<std::string> cumulative_names,
    chromeos_metrics::CumulativeMetrics* cm) {
  const int nbuckets = kMetricsCumulativeTimeOnlineBucketCount;

  int64_t chosen_any = cm->Get(cumulative_names[CHOSEN_ANY]);
  int64_t chosen_cellular = cm->Get(cumulative_names[CHOSEN_CELLULAR]);
  int64_t chosen_wifi = cm->Get(cumulative_names[CHOSEN_WIFI]);

  mli->SendToUMA(histogram_names[CHOSEN_ANY], chosen_any, min_seconds,
                 max_seconds, nbuckets);
  mli->SendToUMA(histogram_names[CHOSEN_CELLULAR], chosen_cellular, min_seconds,
                 max_seconds, nbuckets);
  mli->SendToUMA(histogram_names[CHOSEN_WIFI], chosen_wifi, min_seconds,
                 max_seconds, nbuckets);

  if (chosen_any > 0) {
    mli->SendEnumToUMA(histogram_names[CHOSEN_FRACTION_CELLULAR],
                       chosen_cellular * 100 / chosen_any, 100);
    mli->SendEnumToUMA(histogram_names[CHOSEN_FRACTION_WIFI],
                       chosen_wifi * 100 / chosen_any, 100);
  }
}

void Metrics::OnDefaultServiceChanged(const ServiceRefPtr& logical_service,
                                      bool /* logical_service_changed */,
                                      const ServiceRefPtr& physical_service,
                                      bool /* physical_service_changed */) {
  last_physical_technology_ = physical_service
                                  ? physical_service->technology()
                                  : Technology(Technology::kUnknown);

  base::TimeDelta elapsed_seconds;
  Technology technology = logical_service ? logical_service->technology()
                                          : Technology(Technology::kUnknown);
  if (technology != last_default_technology_) {
    if (last_default_technology_ != Technology::kUnknown) {
      string histogram = GetFullMetricName(kMetricTimeOnlineSecondsSuffix,
                                           last_default_technology_);
      time_online_timer_->GetElapsedTime(&elapsed_seconds);
      SendToUMA(histogram, elapsed_seconds.InSeconds(),
                kMetricTimeOnlineSecondsMin, kMetricTimeOnlineSecondsMax,
                kTimerHistogramNumBuckets);
    }
    last_default_technology_ = technology;
    time_online_timer_->Start();
  }

  // Only consider transitions from online to offline and vice-versa; i.e.
  // ignore switching between wired and wireless or wireless and cellular.
  // TimeToDrop measures time online regardless of how we are connected.
  bool staying_online = ((logical_service != nullptr) && was_last_online_);
  bool staying_offline = ((logical_service == nullptr) && !was_last_online_);
  if (staying_online || staying_offline)
    return;

  if (logical_service == nullptr) {
    time_to_drop_timer_->GetElapsedTime(&elapsed_seconds);
    SendToUMA(kMetricTimeToDropSeconds, elapsed_seconds.InSeconds(),
              kMetricTimeToDropSecondsMin, kMetricTimeToDropSecondsMax,
              kTimerHistogramNumBuckets);
  } else {
    time_to_drop_timer_->Start();
  }

  was_last_online_ = (logical_service != nullptr);
}

void Metrics::NotifyServiceStateChanged(const Service& service,
                                        Service::ConnectState new_state) {
  ServiceMetricsLookupMap::iterator it = services_metrics_.find(&service);
  if (it == services_metrics_.end()) {
    SLOG(this, 1) << "service not found";
    DCHECK(false);
    return;
  }
  ServiceMetrics* service_metrics = it->second.get();
  UpdateServiceStateTransitionMetrics(service_metrics, new_state);

  if (new_state == Service::kStateFailure)
    SendServiceFailure(service);

  if (collect_bootstats_) {
    bootstat_log(base::StringPrintf("network-%s-%s",
                                    service.technology().GetName().c_str(),
                                    service.GetStateString().c_str())
                     .c_str());
  }

  if (new_state != Service::kStateConnected)
    return;

  base::TimeDelta time_resume_to_ready;
  time_resume_to_ready_timer_->GetElapsedTime(&time_resume_to_ready);
  time_resume_to_ready_timer_->Reset();
  service.SendPostReadyStateMetrics(time_resume_to_ready.InMilliseconds());
}

string Metrics::GetFullMetricName(const char* metric_suffix,
                                  Technology technology_id) {
  string technology = technology_id.GetName();
  technology[0] = base::ToUpperASCII(technology[0]);
  return base::StringPrintf("%s.%s.%s", kMetricPrefix, technology.c_str(),
                            metric_suffix);
}

string Metrics::GetSuspendDurationMetricNameFromStatus(
    WiFiConnectionStatusAfterWake status) {
  switch (status) {
    case kWiFiConnectionStatusAfterWakeWoWOnConnected:
      return kMetricSuspendDurationWoWOnConnected;

    case kWiFiConnectionStatusAfterWakeWoWOnDisconnected:
      return kMetricSuspendDurationWoWOnDisconnected;

    case kWiFiConnectionStatusAfterWakeWoWOffConnected:
      return kMetricSuspendDurationWoWOffConnected;

    case kWiFiConnectionStatusAfterWakeWoWOffDisconnected:
      return kMetricSuspendDurationWoWOffDisconnected;

    default:
      // The return type is std::string, which cannot
      // be assigned NULL. Return an empty string instead.
      std::string value;
      return value;
  }
}

void Metrics::NotifyServiceDisconnect(const Service& service) {
  Technology technology = service.technology();
  string histogram = GetFullMetricName(kMetricDisconnectSuffix, technology);
  SendToUMA(histogram, service.explicitly_disconnected(), kMetricDisconnectMin,
            kMetricDisconnectMax, kMetricDisconnectNumBuckets);
}

void Metrics::NotifySignalAtDisconnect(const Service& service,
                                       int16_t signal_strength) {
  // Negate signal_strength (goes from dBm to -dBm) because the metrics don't
  // seem to handle negative values well.  Now everything's positive.
  Technology technology = service.technology();
  string histogram =
      GetFullMetricName(kMetricSignalAtDisconnectSuffix, technology);
  SendToUMA(histogram, -signal_strength, kMetricSignalAtDisconnectMin,
            kMetricSignalAtDisconnectMax, kMetricSignalAtDisconnectNumBuckets);
}

void Metrics::NotifySuspendDone() {
  time_resume_to_ready_timer_->Start();
}

void Metrics::NotifyWakeOnWiFiFeaturesEnabledState(
    WakeOnWiFiFeaturesEnabledState state) {
  SendEnumToUMA(kMetricWakeOnWiFiFeaturesEnabledState, state,
                kWakeOnWiFiFeaturesEnabledStateMax);
}

void Metrics::NotifyVerifyWakeOnWiFiSettingsResult(
    VerifyWakeOnWiFiSettingsResult result) {
  SendEnumToUMA(kMetricVerifyWakeOnWiFiSettingsResult, result,
                kVerifyWakeOnWiFiSettingsResultMax);
}

void Metrics::NotifyConnectedToServiceAfterWake(
    WiFiConnectionStatusAfterWake status) {
  SendEnumToUMA(kMetricWiFiConnectionStatusAfterWake, status,
                kWiFiConnectionStatusAfterWakeMax);
}

void Metrics::NotifySuspendDurationAfterWake(
    WiFiConnectionStatusAfterWake status, int seconds_in_suspend) {
  string metric = GetSuspendDurationMetricNameFromStatus(status);

  if (!metric.empty()) {
    SendToUMA(metric, seconds_in_suspend, kSuspendDurationMin,
              kSuspendDurationMax, kSuspendDurationNumBuckets);
  }
}

void Metrics::NotifyTerminationActionsStarted() {
  if (time_termination_actions_timer->HasStarted())
    return;
  time_termination_actions_timer->Start();
}

void Metrics::NotifyTerminationActionsCompleted(bool success) {
  if (!time_termination_actions_timer->HasStarted())
    return;

  TerminationActionResult result = success ? kTerminationActionResultSuccess
                                           : kTerminationActionResultFailure;

  base::TimeDelta elapsed_time;
  time_termination_actions_timer->GetElapsedTime(&elapsed_time);
  time_termination_actions_timer->Reset();
  string time_metric, result_metric;
  time_metric = kMetricTerminationActionTimeTaken;
  result_metric = kMetricTerminationActionResult;

  SendToUMA(time_metric, elapsed_time.InMilliseconds(),
            kMetricTerminationActionTimeTakenMillisecondsMin,
            kMetricTerminationActionTimeTakenMillisecondsMax,
            kTimerHistogramNumBuckets);

  SendEnumToUMA(result_metric, result, kTerminationActionResultMax);
}

void Metrics::NotifySuspendActionsStarted() {
  if (time_suspend_actions_timer->HasStarted())
    return;
  time_suspend_actions_timer->Start();
  wake_on_wifi_throttled_ = false;
}

void Metrics::NotifySuspendActionsCompleted(bool success) {
  if (!time_suspend_actions_timer->HasStarted())
    return;

  // Reset for next dark resume.
  wake_reason_received_ = false;

  SuspendActionResult result =
      success ? kSuspendActionResultSuccess : kSuspendActionResultFailure;

  base::TimeDelta elapsed_time;
  time_suspend_actions_timer->GetElapsedTime(&elapsed_time);
  time_suspend_actions_timer->Reset();
  string time_metric, result_metric;
  time_metric = kMetricSuspendActionTimeTaken;
  result_metric = kMetricSuspendActionResult;

  SendToUMA(time_metric, elapsed_time.InMilliseconds(),
            kMetricSuspendActionTimeTakenMillisecondsMin,
            kMetricSuspendActionTimeTakenMillisecondsMax,
            kTimerHistogramNumBuckets);

  SendEnumToUMA(result_metric, result, kSuspendActionResultMax);
}

void Metrics::NotifyDarkResumeActionsStarted() {
  if (time_dark_resume_actions_timer->HasStarted())
    return;
  time_dark_resume_actions_timer->Start();
  num_scan_results_expected_in_dark_resume_ = 0;
  dark_resume_scan_retries_ = 0;
}

void Metrics::NotifyDarkResumeActionsCompleted(bool success) {
  if (!time_dark_resume_actions_timer->HasStarted())
    return;

  // Reset for next dark resume.
  wake_reason_received_ = false;

  DarkResumeActionResult result =
      success ? kDarkResumeActionResultSuccess : kDarkResumeActionResultFailure;

  base::TimeDelta elapsed_time;
  time_dark_resume_actions_timer->GetElapsedTime(&elapsed_time);
  time_dark_resume_actions_timer->Reset();

  SendToUMA(kMetricDarkResumeActionTimeTaken, elapsed_time.InMilliseconds(),
            kMetricDarkResumeActionTimeTakenMillisecondsMin,
            kMetricDarkResumeActionTimeTakenMillisecondsMax,
            kTimerHistogramNumBuckets);

  SendEnumToUMA(kMetricDarkResumeActionResult, result,
                kDarkResumeActionResultMax);

  DarkResumeUnmatchedScanResultReceived unmatched_scan_results_received =
      (num_scan_results_expected_in_dark_resume_ < 0)
          ? kDarkResumeUnmatchedScanResultsReceivedTrue
          : kDarkResumeUnmatchedScanResultsReceivedFalse;
  SendEnumToUMA(kMetricDarkResumeUnmatchedScanResultReceived,
                unmatched_scan_results_received,
                kDarkResumeUnmatchedScanResultsReceivedMax);

  SendToUMA(kMetricDarkResumeScanNumRetries, dark_resume_scan_retries_,
            kMetricDarkResumeScanNumRetriesMin,
            kMetricDarkResumeScanNumRetriesMax, kTimerHistogramNumBuckets);
}

void Metrics::NotifyDarkResumeInitiateScan() {
  ++num_scan_results_expected_in_dark_resume_;
}

void Metrics::NotifyDarkResumeScanResultsReceived() {
  --num_scan_results_expected_in_dark_resume_;
}

void Metrics::NotifyLinkMonitorFailure(Technology technology,
                                       LinkMonitorFailure failure,
                                       int seconds_to_failure,
                                       int broadcast_error_count,
                                       int unicast_error_count) {
  string histogram =
      GetFullMetricName(kMetricLinkMonitorFailureSuffix, technology);
  SendEnumToUMA(histogram, failure, kLinkMonitorFailureMax);

  if (failure == kLinkMonitorFailureThresholdReached) {
    if (seconds_to_failure > kMetricLinkMonitorSecondsToFailureMax) {
      seconds_to_failure = kMetricLinkMonitorSecondsToFailureMax;
    }
    histogram =
        GetFullMetricName(kMetricLinkMonitorSecondsToFailureSuffix, technology);
    SendToUMA(histogram, seconds_to_failure,
              kMetricLinkMonitorSecondsToFailureMin,
              kMetricLinkMonitorSecondsToFailureMax,
              kMetricLinkMonitorSecondsToFailureNumBuckets);
    histogram = GetFullMetricName(
        kMetricLinkMonitorBroadcastErrorsAtFailureSuffix, technology);
    SendToUMA(histogram, broadcast_error_count, kMetricLinkMonitorErrorCountMin,
              kMetricLinkMonitorErrorCountMax,
              kMetricLinkMonitorErrorCountNumBuckets);
    histogram = GetFullMetricName(
        kMetricLinkMonitorUnicastErrorsAtFailureSuffix, technology);
    SendToUMA(histogram, unicast_error_count, kMetricLinkMonitorErrorCountMin,
              kMetricLinkMonitorErrorCountMax,
              kMetricLinkMonitorErrorCountNumBuckets);
  }
}

void Metrics::NotifyLinkMonitorResponseTimeSampleAdded(
    Technology technology, int response_time_milliseconds) {
  string histogram =
      GetFullMetricName(kMetricLinkMonitorResponseTimeSampleSuffix, technology);
  SendToUMA(histogram, response_time_milliseconds,
            kMetricLinkMonitorResponseTimeSampleMin,
            kMetricLinkMonitorResponseTimeSampleMax,
            kMetricLinkMonitorResponseTimeSampleNumBuckets);
}

void Metrics::NotifyNeighborLinkMonitorFailure(
    Technology technology,
    IPAddress::Family family,
    patchpanel::NeighborConnectedStateChangedSignal::Role role) {
  string histogram =
      GetFullMetricName(kMetricNeighborLinkMonitorFailureSuffix, technology);
  NeighborLinkMonitorFailure failure = kNeighborLinkMonitorFailureUnknown;
  using NeighborSignal = patchpanel::NeighborConnectedStateChangedSignal;
  if (family == IPAddress::kFamilyIPv4) {
    switch (role) {
      case NeighborSignal::GATEWAY:
        failure = kNeighborIPv4GatewayFailure;
        break;
      case NeighborSignal::DNS_SERVER:
        failure = kNeighborIPv4DNSServerFailure;
        break;
      case NeighborSignal::GATEWAY_AND_DNS_SERVER:
        failure = kNeighborIPv4GatewayAndDNSServerFailure;
        break;
      default:
        failure = kNeighborLinkMonitorFailureUnknown;
    }
  } else if (family == IPAddress::kFamilyIPv6) {
    switch (role) {
      case NeighborSignal::GATEWAY:
        failure = kNeighborIPv6GatewayFailure;
        break;
      case NeighborSignal::DNS_SERVER:
        failure = kNeighborIPv6DNSServerFailure;
        break;
      case NeighborSignal::GATEWAY_AND_DNS_SERVER:
        failure = kNeighborIPv6GatewayAndDNSServerFailure;
        break;
      default:
        failure = kNeighborLinkMonitorFailureUnknown;
    }
  } else {
    LOG(ERROR) << __func__ << " with kFamilyUnknown";
    return;
  }

  SendEnumToUMA(histogram, failure, kNeighborLinkMonitorFailureMax);
}

void Metrics::NotifyApChannelSwitch(uint16_t frequency,
                                    uint16_t new_frequency) {
  WiFiChannel channel = WiFiFrequencyToChannel(frequency);
  WiFiChannel new_channel = WiFiFrequencyToChannel(new_frequency);
  WiFiFrequencyRange range = WiFiChannelToFrequencyRange(channel);
  WiFiFrequencyRange new_range = WiFiChannelToFrequencyRange(new_channel);
  WiFiApChannelSwitch channel_switch = kWiFiApChannelSwitchUndef;
  if (range == kWiFiFrequencyRange24 && new_range == kWiFiFrequencyRange24) {
    channel_switch = kWiFiApChannelSwitch24To24;
  } else if (range == kWiFiFrequencyRange24 &&
             new_range == kWiFiFrequencyRange5) {
    channel_switch = kWiFiApChannelSwitch24To5;
  } else if (range == kWiFiFrequencyRange5 &&
             new_range == kWiFiFrequencyRange24) {
    channel_switch = kWiFiApChannelSwitch5To24;
  } else if (range == kWiFiFrequencyRange5 &&
             new_range == kWiFiFrequencyRange5) {
    channel_switch = kWiFiApChannelSwitch5To5;
  }
  SendEnumToUMA(kMetricApChannelSwitch, channel_switch,
                kWiFiApChannelSwitchMax);
}

void Metrics::NotifyAp80211kSupport(bool neighbor_list_supported) {
  WiFiAp80211kSupport support = kWiFiAp80211kNone;
  if (neighbor_list_supported) {
    support = kWiFiAp80211kNeighborList;
  }
  SendEnumToUMA(kMetricAp80211kSupport, support, kWiFiAp80211kMax);
}

void Metrics::NotifyAp80211rSupport(bool ota_ft_supported,
                                    bool otds_ft_supported) {
  WiFiAp80211rSupport support = kWiFiAp80211rNone;
  if (otds_ft_supported) {
    support = kWiFiAp80211rOTDS;
  } else if (ota_ft_supported) {
    support = kWiFiAp80211rOTA;
  }
  SendEnumToUMA(kMetricAp80211rSupport, support, kWiFiAp80211rMax);
}

void Metrics::NotifyAp80211vDMSSupport(bool dms_supported) {
  WiFiAp80211vDMSSupport support = kWiFiAp80211vNoDMS;
  if (dms_supported) {
    support = kWiFiAp80211vDMS;
  }
  SendEnumToUMA(kMetricAp80211vDMSSupport, support, kWiFiAp80211vDMSMax);
}

void Metrics::NotifyAp80211vBSSMaxIdlePeriodSupport(
    bool bss_max_idle_period_supported) {
  WiFiAp80211vBSSMaxIdlePeriodSupport support = kWiFiAp80211vNoBSSMaxIdlePeriod;
  if (bss_max_idle_period_supported) {
    support = kWiFiAp80211vBSSMaxIdlePeriod;
  }
  SendEnumToUMA(kMetricAp80211vBSSMaxIdlePeriodSupport, support,
                kWiFiAp80211vBSSMaxIdlePeriodMax);
}

void Metrics::NotifyAp80211vBSSTransitionSupport(
    bool bss_transition_supported) {
  WiFiAp80211vBSSTransitionSupport support = kWiFiAp80211vNoBSSTransition;
  if (bss_transition_supported) {
    support = kWiFiAp80211vBSSTransition;
  }
  SendEnumToUMA(kMetricAp80211vBSSTransitionSupport, support,
                kWiFiAp80211vBSSTransitionMax);
}

#if !defined(DISABLE_WIFI)
void Metrics::Notify80211Disconnect(WiFiDisconnectByWhom by_whom,
                                    IEEE_80211::WiFiReasonCode reason) {
  string metric_disconnect_reason;
  string metric_disconnect_type;
  WiFiReasonType type;

  if (by_whom == kDisconnectedByAp) {
    metric_disconnect_reason = kMetricLinkApDisconnectReason;
    metric_disconnect_type = kMetricLinkApDisconnectType;
    type = kReasonCodeTypeByAp;
  } else {
    metric_disconnect_reason = kMetricLinkClientDisconnectReason;
    metric_disconnect_type = kMetricLinkClientDisconnectType;
    switch (reason) {
      case IEEE_80211::kReasonCodeSenderHasLeft:
      case IEEE_80211::kReasonCodeDisassociatedHasLeft:
        type = kReasonCodeTypeByUser;
        break;

      case IEEE_80211::kReasonCodeInactivity:
        type = kReasonCodeTypeConsideredDead;
        break;

      default:
        type = kReasonCodeTypeByClient;
        break;
    }
  }
  SendEnumToUMA(metric_disconnect_reason, reason, IEEE_80211::kReasonCodeMax);
  SendEnumToUMA(metric_disconnect_type, type, kReasonCodeTypeMax);
}
#endif  // DISABLE_WIFI

void Metrics::NotifyWiFiSupplicantAbort() {
  SendToUMA(kMetricWifiSupplicantAttempts,
            kMetricWifiSupplicantAttemptsMax,  // abort == max
            kMetricWifiSupplicantAttemptsMin, kMetricWifiSupplicantAttemptsMax,
            kMetricWifiSupplicantAttemptsNumBuckets);
}

void Metrics::NotifyWiFiSupplicantSuccess(int attempts) {
  // Cap "success" at 1 lower than max. Max means we aborted.
  if (attempts >= kMetricWifiSupplicantAttemptsMax)
    attempts = kMetricWifiSupplicantAttemptsMax - 1;

  SendToUMA(kMetricWifiSupplicantAttempts, attempts,
            kMetricWifiSupplicantAttemptsMin, kMetricWifiSupplicantAttemptsMax,
            kMetricWifiSupplicantAttemptsNumBuckets);
}

void Metrics::RegisterDevice(int interface_index, Technology technology) {
  SLOG(this, 2) << __func__ << ": " << interface_index;

  if (collect_bootstats_ && technology.IsPrimaryConnectivityTechnology()) {
    bootstat_log(base::StringPrintf("network-%s-registered",
                                    technology.GetName().c_str())
                     .c_str());
  }

  auto device_metrics = std::make_unique<DeviceMetrics>();
  device_metrics->technology = technology;
  string histogram =
      GetFullMetricName(kMetricTimeToInitializeMillisecondsSuffix, technology);
  device_metrics->initialization_timer.reset(
      new chromeos_metrics::TimerReporter(
          histogram, kMetricTimeToInitializeMillisecondsMin,
          kMetricTimeToInitializeMillisecondsMax,
          kMetricTimeToInitializeMillisecondsNumBuckets));
  device_metrics->initialization_timer->Start();
  histogram =
      GetFullMetricName(kMetricTimeToEnableMillisecondsSuffix, technology);
  device_metrics->enable_timer.reset(new chromeos_metrics::TimerReporter(
      histogram, kMetricTimeToEnableMillisecondsMin,
      kMetricTimeToEnableMillisecondsMax,
      kMetricTimeToEnableMillisecondsNumBuckets));
  histogram =
      GetFullMetricName(kMetricTimeToDisableMillisecondsSuffix, technology);
  device_metrics->disable_timer.reset(new chromeos_metrics::TimerReporter(
      histogram, kMetricTimeToDisableMillisecondsMin,
      kMetricTimeToDisableMillisecondsMax,
      kMetricTimeToDisableMillisecondsNumBuckets));
  histogram =
      GetFullMetricName(kMetricTimeToScanMillisecondsSuffix, technology);
  device_metrics->scan_timer.reset(new chromeos_metrics::TimerReporter(
      histogram, kMetricTimeToScanMillisecondsMin,
      kMetricTimeToScanMillisecondsMax,
      kMetricTimeToScanMillisecondsNumBuckets));
  histogram =
      GetFullMetricName(kMetricTimeToConnectMillisecondsSuffix, technology);
  device_metrics->connect_timer.reset(new chromeos_metrics::TimerReporter(
      histogram, kMetricTimeToConnectMillisecondsMin,
      kMetricTimeToConnectMillisecondsMax,
      kMetricTimeToConnectMillisecondsNumBuckets));
  histogram = GetFullMetricName(kMetricTimeToScanAndConnectMillisecondsSuffix,
                                technology);
  device_metrics->scan_connect_timer.reset(new chromeos_metrics::TimerReporter(
      histogram, kMetricTimeToScanMillisecondsMin,
      kMetricTimeToScanMillisecondsMax + kMetricTimeToConnectMillisecondsMax,
      kMetricTimeToScanMillisecondsNumBuckets +
          kMetricTimeToConnectMillisecondsNumBuckets));
  device_metrics->auto_connect_timer.reset(new chromeos_metrics::TimerReporter(
      kMetricCellularAutoConnectTotalTime,
      kMetricCellularAutoConnectTotalTimeMin,
      kMetricCellularAutoConnectTotalTimeMax,
      kMetricCellularAutoConnectTotalTimeNumBuckets));
  devices_metrics_[interface_index] = std::move(device_metrics);
}

bool Metrics::IsDeviceRegistered(int interface_index, Technology technology) {
  SLOG(this, 2) << __func__ << ": interface index: " << interface_index
                << ", technology: " << technology;
  DeviceMetrics* device_metrics = GetDeviceMetrics(interface_index);
  if (device_metrics == nullptr)
    return false;
  // Make sure the device technologies match.
  return (technology == device_metrics->technology);
}

void Metrics::DeregisterDevice(int interface_index) {
  SLOG(this, 2) << __func__ << ": interface index: " << interface_index;

  DeviceMetrics* device_metrics = GetDeviceMetrics(interface_index);
  if (device_metrics != nullptr) {
    NotifyDeviceRemovedEvent(device_metrics->technology);
  }

  devices_metrics_.erase(interface_index);
}

void Metrics::NotifyDeviceInitialized(int interface_index) {
  DeviceMetrics* device_metrics = GetDeviceMetrics(interface_index);
  if (device_metrics == nullptr)
    return;
  if (!device_metrics->initialization_timer->Stop())
    return;
  device_metrics->initialization_timer->ReportMilliseconds();
}

void Metrics::NotifyDeviceEnableStarted(int interface_index) {
  DeviceMetrics* device_metrics = GetDeviceMetrics(interface_index);
  if (device_metrics == nullptr)
    return;
  device_metrics->enable_timer->Start();
}

void Metrics::NotifyDeviceEnableFinished(int interface_index) {
  DeviceMetrics* device_metrics = GetDeviceMetrics(interface_index);
  if (device_metrics == nullptr)
    return;
  if (!device_metrics->enable_timer->Stop())
    return;
  device_metrics->enable_timer->ReportMilliseconds();
}

void Metrics::NotifyDeviceDisableStarted(int interface_index) {
  DeviceMetrics* device_metrics = GetDeviceMetrics(interface_index);
  if (device_metrics == nullptr)
    return;
  device_metrics->disable_timer->Start();
}

void Metrics::NotifyDeviceDisableFinished(int interface_index) {
  DeviceMetrics* device_metrics = GetDeviceMetrics(interface_index);
  if (device_metrics == nullptr)
    return;
  if (!device_metrics->disable_timer->Stop())
    return;
  device_metrics->disable_timer->ReportMilliseconds();
}

void Metrics::NotifyDeviceScanStarted(int interface_index) {
  DeviceMetrics* device_metrics = GetDeviceMetrics(interface_index);
  if (device_metrics == nullptr)
    return;
  device_metrics->scan_timer->Start();
  device_metrics->scan_connect_timer->Start();
}

void Metrics::NotifyDeviceScanFinished(int interface_index) {
  DeviceMetrics* device_metrics = GetDeviceMetrics(interface_index);
  if (device_metrics == nullptr)
    return;
  if (!device_metrics->scan_timer->Stop())
    return;
  // Don't send TimeToScan metrics if the elapsed time exceeds the max metrics
  // value.  Huge scan times usually mean something's gone awry; for cellular,
  // for instance, this usually means that the modem is in an area without
  // service and we're not interested in this scenario.
  base::TimeDelta elapsed_time;
  device_metrics->scan_timer->GetElapsedTime(&elapsed_time);
  if (elapsed_time.InMilliseconds() <= kMetricTimeToScanMillisecondsMax)
    device_metrics->scan_timer->ReportMilliseconds();
}

void Metrics::ResetScanTimer(int interface_index) {
  DeviceMetrics* device_metrics = GetDeviceMetrics(interface_index);
  if (device_metrics == nullptr)
    return;
  device_metrics->scan_timer->Reset();
}

void Metrics::NotifyDeviceConnectStarted(int interface_index,
                                         bool is_auto_connecting) {
  DeviceMetrics* device_metrics = GetDeviceMetrics(interface_index);
  if (device_metrics == nullptr)
    return;
  device_metrics->connect_timer->Start();

  if (is_auto_connecting) {
    device_metrics->auto_connect_tries++;
    if (device_metrics->auto_connect_tries == 1)
      device_metrics->auto_connect_timer->Start();
  } else {
    AutoConnectMetricsReset(device_metrics);
  }
}

void Metrics::NotifyDeviceConnectFinished(int interface_index) {
  DeviceMetrics* device_metrics = GetDeviceMetrics(interface_index);
  if (device_metrics == nullptr)
    return;
  if (!device_metrics->connect_timer->Stop())
    return;
  device_metrics->connect_timer->ReportMilliseconds();

  if (device_metrics->auto_connect_tries > 0) {
    if (!device_metrics->auto_connect_timer->Stop())
      return;
    base::TimeDelta elapsed_time;
    device_metrics->auto_connect_timer->GetElapsedTime(&elapsed_time);
    if (elapsed_time.InMilliseconds() > kMetricCellularAutoConnectTotalTimeMax)
      return;
    device_metrics->auto_connect_timer->ReportMilliseconds();
    SendToUMA(
        kMetricCellularAutoConnectTries, device_metrics->auto_connect_tries,
        kMetricCellularAutoConnectTriesMin, kMetricCellularAutoConnectTriesMax,
        kMetricCellularAutoConnectTriesNumBuckets);
    AutoConnectMetricsReset(device_metrics);
  }

  if (!device_metrics->scan_connect_timer->Stop())
    return;
  device_metrics->scan_connect_timer->ReportMilliseconds();
}

void Metrics::ResetConnectTimer(int interface_index) {
  DeviceMetrics* device_metrics = GetDeviceMetrics(interface_index);
  if (device_metrics == nullptr)
    return;
  device_metrics->connect_timer->Reset();
  device_metrics->scan_connect_timer->Reset();
}

void Metrics::Notify3GPPRegistrationDelayedDropPosted() {
  SendEnumToUMA(kMetricCellular3GPPRegistrationDelayedDrop,
                kCellular3GPPRegistrationDelayedDropPosted,
                kCellular3GPPRegistrationDelayedDropMax);
}

void Metrics::Notify3GPPRegistrationDelayedDropCanceled() {
  SendEnumToUMA(kMetricCellular3GPPRegistrationDelayedDrop,
                kCellular3GPPRegistrationDelayedDropCanceled,
                kCellular3GPPRegistrationDelayedDropMax);
}

void Metrics::NotifyCellularDeviceDrop(const string& network_technology,
                                       uint16_t signal_strength) {
  SLOG(this, 2) << __func__ << ": " << network_technology << ", "
                << signal_strength;
  CellularDropTechnology drop_technology = kCellularDropTechnologyUnknown;
  if (network_technology == kNetworkTechnology1Xrtt) {
    drop_technology = kCellularDropTechnology1Xrtt;
  } else if (network_technology == kNetworkTechnologyEdge) {
    drop_technology = kCellularDropTechnologyEdge;
  } else if (network_technology == kNetworkTechnologyEvdo) {
    drop_technology = kCellularDropTechnologyEvdo;
  } else if (network_technology == kNetworkTechnologyGprs) {
    drop_technology = kCellularDropTechnologyGprs;
  } else if (network_technology == kNetworkTechnologyGsm) {
    drop_technology = kCellularDropTechnologyGsm;
  } else if (network_technology == kNetworkTechnologyHspa) {
    drop_technology = kCellularDropTechnologyHspa;
  } else if (network_technology == kNetworkTechnologyHspaPlus) {
    drop_technology = kCellularDropTechnologyHspaPlus;
  } else if (network_technology == kNetworkTechnologyLte) {
    drop_technology = kCellularDropTechnologyLte;
  } else if (network_technology == kNetworkTechnologyUmts) {
    drop_technology = kCellularDropTechnologyUmts;
  }
  SendEnumToUMA(kMetricCellularDrop, drop_technology,
                kCellularDropTechnologyMax);
  SendToUMA(kMetricCellularSignalStrengthBeforeDrop, signal_strength,
            kMetricCellularSignalStrengthBeforeDropMin,
            kMetricCellularSignalStrengthBeforeDropMax,
            kMetricCellularSignalStrengthBeforeDropNumBuckets);
}

void Metrics::NotifyCellularDeviceConnectionFailure() {
  library_->SendEnumToUMA(kMetricCellularFailure,
                          kMetricCellularConnectionFailure,
                          kMetricCellularMaxFailure);
}

void Metrics::NotifyCellularDeviceDisconnectionFailure() {
  library_->SendEnumToUMA(kMetricCellularFailure,
                          kMetricCellularDisconnectionFailure,
                          kMetricCellularMaxFailure);
}

void Metrics::NotifyCellularOutOfCredits(
    Metrics::CellularOutOfCreditsReason reason) {
  SendEnumToUMA(kMetricCellularOutOfCreditsReason, reason,
                kCellularOutOfCreditsReasonMax);
}

void Metrics::NotifyCorruptedProfile() {
  SendEnumToUMA(kMetricCorruptedProfile, kCorruptedProfile,
                kCorruptedProfileMax);
}

void Metrics::NotifyWifiAutoConnectableServices(int num_services) {
  SendToUMA(kMetricWifiAutoConnectableServices, num_services,
            kMetricWifiAutoConnectableServicesMin,
            kMetricWifiAutoConnectableServicesMax,
            kMetricWifiAutoConnectableServicesNumBuckets);
}

void Metrics::NotifyWifiAvailableBSSes(int num_bss) {
  SendToUMA(kMetricWifiAvailableBSSes, num_bss, kMetricWifiAvailableBSSesMin,
            kMetricWifiAvailableBSSesMax, kMetricWifiAvailableBSSesNumBuckets);
}

void Metrics::NotifyServicesOnSameNetwork(int num_services) {
  SendToUMA(kMetricServicesOnSameNetwork, num_services,
            kMetricServicesOnSameNetworkMin, kMetricServicesOnSameNetworkMax,
            kMetricServicesOnSameNetworkNumBuckets);
}

void Metrics::NotifyUserInitiatedEvent(int event) {
  SendEnumToUMA(kMetricUserInitiatedEvents, event, kUserInitiatedEventMax);
}

void Metrics::NotifyWifiTxBitrate(int bitrate) {
  SendToUMA(kMetricWifiTxBitrate, bitrate, kMetricWifiTxBitrateMin,
            kMetricWifiTxBitrateMax, kMetricWifiTxBitrateNumBuckets);
}

void Metrics::NotifyUserInitiatedConnectionResult(const string& name,
                                                  int result) {
  SendEnumToUMA(name, result, kUserInitiatedConnectionResultMax);
}

void Metrics::NotifyUserInitiatedConnectionFailureReason(
    const string& name, const Service::ConnectFailure failure) {
  UserInitiatedConnectionFailureReason reason;
  switch (failure) {
    case Service::kFailureNone:
      reason = kUserInitiatedConnectionFailureReasonNone;
      break;
    case Service::kFailureBadPassphrase:
      reason = kUserInitiatedConnectionFailureReasonBadPassphrase;
      break;
    case Service::kFailureBadWEPKey:
      reason = kUserInitiatedConnectionFailureReasonBadWEPKey;
      break;
    case Service::kFailureConnect:
      reason = kUserInitiatedConnectionFailureReasonConnect;
      break;
    case Service::kFailureDHCP:
      reason = kUserInitiatedConnectionFailureReasonDHCP;
      break;
    case Service::kFailureDNSLookup:
      reason = kUserInitiatedConnectionFailureReasonDNSLookup;
      break;
    case Service::kFailureEAPAuthentication:
      reason = kUserInitiatedConnectionFailureReasonEAPAuthentication;
      break;
    case Service::kFailureEAPLocalTLS:
      reason = kUserInitiatedConnectionFailureReasonEAPLocalTLS;
      break;
    case Service::kFailureEAPRemoteTLS:
      reason = kUserInitiatedConnectionFailureReasonEAPRemoteTLS;
      break;
    case Service::kFailureNotAssociated:
      reason = kUserInitiatedConnectionFailureReasonNotAssociated;
      break;
    case Service::kFailureNotAuthenticated:
      reason = kUserInitiatedConnectionFailureReasonNotAuthenticated;
      break;
    case Service::kFailureOutOfRange:
      reason = kUserInitiatedConnectionFailureReasonOutOfRange;
      break;
    case Service::kFailurePinMissing:
      reason = kUserInitiatedConnectionFailureReasonPinMissing;
      break;
    case Service::kFailureTooManySTAs:
      reason = kUserInitiatedConnectionFailureReasonTooManySTAs;
      break;
    default:
      reason = kUserInitiatedConnectionFailureReasonUnknown;
      break;
  }
  SendEnumToUMA(name, reason, kUserInitiatedConnectionFailureReasonMax);
}

void Metrics::NotifyFallbackDNSTestResult(Technology technology_id,
                                          int result) {
  string histogram =
      GetFullMetricName(kMetricFallbackDNSTestResultSuffix, technology_id);
  SendEnumToUMA(histogram, result, kFallbackDNSTestResultMax);
}

void Metrics::NotifyNetworkProblemDetected(Technology technology_id,
                                           int reason) {
  string histogram =
      GetFullMetricName(kMetricNetworkProblemDetectedSuffix, technology_id);
  SendEnumToUMA(histogram, reason, kNetworkProblemMax);
}

void Metrics::NotifyDeviceConnectionStatus(ConnectionStatus status) {
  SendEnumToUMA(kMetricDeviceConnectionStatus, status, kConnectionStatusMax);
}

void Metrics::NotifyDhcpClientStatus(DhcpClientStatus status) {
  SendEnumToUMA(kMetricDhcpClientStatus, status, kDhcpClientStatusMax);
}

void Metrics::NotifyNetworkConnectionIPType(Technology technology_id,
                                            NetworkConnectionIPType type) {
  string histogram =
      GetFullMetricName(kMetricNetworkConnectionIPTypeSuffix, technology_id);
  SendEnumToUMA(histogram, type, kNetworkConnectionIPTypeMax);
}

void Metrics::NotifyIPv6ConnectivityStatus(Technology technology_id,
                                           bool status) {
  string histogram =
      GetFullMetricName(kMetricIPv6ConnectivityStatusSuffix, technology_id);
  IPv6ConnectivityStatus ipv6_status =
      status ? kIPv6ConnectivityStatusYes : kIPv6ConnectivityStatusNo;
  SendEnumToUMA(histogram, ipv6_status, kIPv6ConnectivityStatusMax);
}

void Metrics::NotifyDevicePresenceStatus(Technology technology_id,
                                         bool status) {
  string histogram =
      GetFullMetricName(kMetricDevicePresenceStatusSuffix, technology_id);
  DevicePresenceStatus presence =
      status ? kDevicePresenceStatusYes : kDevicePresenceStatusNo;
  SendEnumToUMA(histogram, presence, kDevicePresenceStatusMax);
}

void Metrics::NotifyDeviceRemovedEvent(Technology technology_id) {
  DeviceTechnologyType type;
  switch (technology_id) {
    case Technology::kEthernet:
      type = kDeviceTechnologyTypeEthernet;
      break;
    case Technology::kWifi:
      type = kDeviceTechnologyTypeWifi;
      break;
    case Technology::kCellular:
      type = kDeviceTechnologyTypeCellular;
      break;
    default:
      type = kDeviceTechnologyTypeUnknown;
      break;
  }
  SendEnumToUMA(kMetricDeviceRemovedEvent, type, kDeviceTechnologyTypeMax);
}

void Metrics::NotifyUnreliableLinkSignalStrength(Technology technology_id,
                                                 int signal_strength) {
  string histogram = GetFullMetricName(
      kMetricUnreliableLinkSignalStrengthSuffix, technology_id);
  SendToUMA(histogram, signal_strength, kMetricServiceSignalStrengthMin,
            kMetricServiceSignalStrengthMax,
            kMetricServiceSignalStrengthNumBuckets);
}

bool Metrics::SendEnumToUMA(const string& name, int sample, int max) {
  SLOG(this, 5) << "Sending enum " << name << " with value " << sample << ".";
  return library_->SendEnumToUMA(name, sample, max);
}

bool Metrics::SendToUMA(
    const string& name, int sample, int min, int max, int num_buckets) {
  SLOG(this, 5) << "Sending metric " << name << " with value " << sample << ".";
  return library_->SendToUMA(name, sample, min, max, num_buckets);
}

bool Metrics::SendSparseToUMA(const string& name, int sample) {
  SLOG(this, 5) << "Sending sparse metric " << name << " with value " << sample
                << ".";
  return library_->SendSparseToUMA(name, sample);
}

void Metrics::NotifyWakeOnWiFiThrottled() {
  wake_on_wifi_throttled_ = true;
}

void Metrics::NotifySuspendWithWakeOnWiFiEnabledDone() {
  WakeOnWiFiThrottled throttled_result = wake_on_wifi_throttled_
                                             ? kWakeOnWiFiThrottledTrue
                                             : kWakeOnWiFiThrottledFalse;
  SendEnumToUMA(kMetricWakeOnWiFiThrottled, throttled_result,
                kWakeOnWiFiThrottledMax);
}

void Metrics::NotifyWakeupReasonReceived() {
  wake_reason_received_ = true;
}

#if !defined(DISABLE_WIFI)
// TODO(zqiu): Change argument type from WakeOnWiFi::WakeOnWiFiTrigger to
// Metrics::DarkResumeWakeReason, to remove the dependency for WakeOnWiFi.
// to remove the dependency for WakeOnWiFi.
void Metrics::NotifyWakeOnWiFiOnDarkResume(
    WakeOnWiFi::WakeOnWiFiTrigger reason) {
  WakeReasonReceivedBeforeOnDarkResume result =
      wake_reason_received_ ? kWakeReasonReceivedBeforeOnDarkResumeTrue
                            : kWakeReasonReceivedBeforeOnDarkResumeFalse;

  SendEnumToUMA(kMetricWakeReasonReceivedBeforeOnDarkResume, result,
                kWakeReasonReceivedBeforeOnDarkResumeMax);

  DarkResumeWakeReason wake_reason;
  switch (reason) {
    case WakeOnWiFi::kWakeTriggerPattern:
      wake_reason = kDarkResumeWakeReasonPattern;
      break;
    case WakeOnWiFi::kWakeTriggerDisconnect:
      wake_reason = kDarkResumeWakeReasonDisconnect;
      break;
    case WakeOnWiFi::kWakeTriggerSSID:
      wake_reason = kDarkResumeWakeReasonSSID;
      break;
    case WakeOnWiFi::kWakeTriggerUnsupported:
    default:
      wake_reason = kDarkResumeWakeReasonUnsupported;
      break;
  }
  SendEnumToUMA(kMetricDarkResumeWakeReason, wake_reason,
                kDarkResumeWakeReasonMax);
}
#endif  // DISABLE_WIFI

void Metrics::NotifyScanStartedInDarkResume(bool is_active_scan) {
  DarkResumeScanType scan_type =
      is_active_scan ? kDarkResumeScanTypeActive : kDarkResumeScanTypePassive;
  SendEnumToUMA(kMetricDarkResumeScanType, scan_type, kDarkResumeScanTypeMax);
}

void Metrics::NotifyDarkResumeScanRetry() {
  ++dark_resume_scan_retries_;
}

void Metrics::NotifyBeforeSuspendActions(bool is_connected,
                                         bool in_dark_resume) {
  if (in_dark_resume && dark_resume_scan_retries_) {
    DarkResumeScanRetryResult connect_result =
        is_connected ? kDarkResumeScanRetryResultConnected
                     : kDarkResumeScanRetryResultNotConnected;
    SendEnumToUMA(kMetricDarkResumeScanRetryResult, connect_result,
                  kDarkResumeScanRetryResultMax);
  }
}

void Metrics::NotifyConnectionDiagnosticsIssue(const string& issue) {
  ConnectionDiagnosticsIssue issue_enum;
  if (issue == ConnectionDiagnostics::kIssueIPCollision) {
    issue_enum = kConnectionDiagnosticsIssueIPCollision;
  } else if (issue == ConnectionDiagnostics::kIssueRouting) {
    issue_enum = kConnectionDiagnosticsIssueRouting;
  } else if (issue == ConnectionDiagnostics::kIssueHTTPBrokenPortal) {
    issue_enum = kConnectionDiagnosticsIssueHTTPBrokenPortal;
  } else if (issue == ConnectionDiagnostics::kIssueDNSServerMisconfig) {
    issue_enum = kConnectionDiagnosticsIssueDNSServerMisconfig;
  } else if (issue == ConnectionDiagnostics::kIssueDNSServerNoResponse) {
    issue_enum = kConnectionDiagnosticsIssueDNSServerNoResponse;
  } else if (issue == ConnectionDiagnostics::kIssueNoDNSServersConfigured) {
    issue_enum = kConnectionDiagnosticsIssueNoDNSServersConfigured;
  } else if (issue == ConnectionDiagnostics::kIssueDNSServersInvalid) {
    issue_enum = kConnectionDiagnosticsIssueDNSServersInvalid;
  } else if (issue == ConnectionDiagnostics::kIssueNone) {
    issue_enum = kConnectionDiagnosticsIssueNone;
  } else if (issue == ConnectionDiagnostics::kIssueCaptivePortal) {
    issue_enum = kConnectionDiagnosticsIssueCaptivePortal;
  } else if (issue == ConnectionDiagnostics::kIssueGatewayUpstream) {
    issue_enum = kConnectionDiagnosticsIssueGatewayUpstream;
  } else if (issue == ConnectionDiagnostics::kIssueGatewayNotResponding) {
    issue_enum = kConnectionDiagnosticsIssueGatewayNotResponding;
  } else if (issue == ConnectionDiagnostics::kIssueServerNotResponding) {
    issue_enum = kConnectionDiagnosticsIssueServerNotResponding;
  } else if (issue == ConnectionDiagnostics::kIssueGatewayArpFailed) {
    issue_enum = kConnectionDiagnosticsIssueGatewayArpFailed;
  } else if (issue == ConnectionDiagnostics::kIssueServerArpFailed) {
    issue_enum = kConnectionDiagnosticsIssueServerArpFailed;
  } else if (issue == ConnectionDiagnostics::kIssueInternalError) {
    issue_enum = kConnectionDiagnosticsIssueInternalError;
  } else if (issue == ConnectionDiagnostics::kIssueGatewayNoNeighborEntry) {
    issue_enum = kConnectionDiagnosticsIssueGatewayNoNeighborEntry;
  } else if (issue == ConnectionDiagnostics::kIssueServerNoNeighborEntry) {
    issue_enum = kConnectionDiagnosticsIssueServerNoNeighborEntry;
  } else if (issue ==
             ConnectionDiagnostics::kIssueGatewayNeighborEntryNotConnected) {
    issue_enum = kConnectionDiagnosticsIssueGatewayNeighborEntryNotConnected;
  } else if (issue ==
             ConnectionDiagnostics::kIssueServerNeighborEntryNotConnected) {
    issue_enum = kConnectionDiagnosticsIssueServerNeighborEntryNotConnected;
  } else {
    LOG(ERROR) << __func__ << ": Invalid issue: " << issue;
    return;
  }

  SendEnumToUMA(kMetricConnectionDiagnosticsIssue, issue_enum,
                kConnectionDiagnosticsIssueMax);
}

void Metrics::NotifyPortalDetectionMultiProbeResult(
    const PortalDetector::Result& http_result,
    const PortalDetector::Result& https_result) {
  // kTimeout is implicitly treated as a failure
  // kRedirect on HTTPS is unexpected and ignored
  PortalDetectionMultiProbeResult result_enum;
  if (https_result.status == PortalDetector::Status::kRedirect) {
    result_enum = kPortalDetectionMultiProbeResultUndefined;
  } else if (https_result.status != PortalDetector::Status::kSuccess &&
             http_result.status == PortalDetector::Status::kSuccess) {
    result_enum = kPortalDetectionMultiProbeResultHTTPSBlockedHTTPUnblocked;
  } else if (https_result.status != PortalDetector::Status::kSuccess &&
             http_result.status == PortalDetector::Status::kRedirect) {
    result_enum = kPortalDetectionMultiProbeResultHTTPSBlockedHTTPRedirected;
  } else if (https_result.status != PortalDetector::Status::kSuccess) {
    result_enum = kPortalDetectionMultiProbeResultHTTPSBlockedHTTPBlocked;
  } else if (https_result.status == PortalDetector::Status::kSuccess &&
             http_result.status == PortalDetector::Status::kSuccess) {
    result_enum = kPortalDetectionMultiProbeResultHTTPSUnblockedHTTPUnblocked;
  } else if (https_result.status == PortalDetector::Status::kSuccess &&
             http_result.status == PortalDetector::Status::kRedirect) {
    result_enum = kPortalDetectionMultiProbeResultHTTPSUnblockedHTTPRedirected;
  } else {
    result_enum = kPortalDetectionMultiProbeResultHTTPSUnblockedHTTPBlocked;
  }

  SendEnumToUMA(kMetricPortalDetectionMultiProbeResult, result_enum,
                kPortalDetectionMultiProbeResultMax);
}

void Metrics::NotifyHS20Support(bool hs20_supported, int hs20_version_number) {
  if (!hs20_supported) {
    SendEnumToUMA(kMetricHS20Support, kHS20Unsupported, kHS20SupportMax);
    return;
  }
  int hotspot_version = kHS20VersionInvalid;
  switch (hs20_version_number) {
    // Valid values.
    case 1:
      hotspot_version = kHS20Version1;
      break;
    case 2:
      hotspot_version = kHS20Version2;
      break;
    case 3:
      hotspot_version = kHS20Version3;
      break;
    // Invalid values.
    default:
      break;
  }
  SendEnumToUMA(kMetricHS20Support, hotspot_version, kHS20SupportMax);
}

// static
int Metrics::GetRegulatoryDomainValue(std::string country_code) {
  // Convert country code to upper case before checking validity.
  country_code = base::ToUpperASCII(country_code);

  // Check if alpha2 attribute is a valid counrty code.
  // All combinations of 2 uppercase letters + "00" are considered valid.
  if (country_code == "00") {
    return kRegDom00;
  } else if (country_code.length() != 2 || !std::isupper(country_code[0]) ||
             !std::isupper(country_code[1])) {
    return kCountryCodeInvalid;
  } else {
    // Calculate corresponding country code value for UMA histogram.
    return ((static_cast<int>(country_code[0]) - static_cast<int>('A')) * 26) +
           (static_cast<int>(country_code[1]) - static_cast<int>('A') + 2);
  }
}

void Metrics::InitializeCommonServiceMetrics(const Service& service) {
  Technology technology = service.technology();
  string histogram =
      GetFullMetricName(kMetricTimeToConfigMillisecondsSuffix, technology);
  AddServiceStateTransitionTimer(service, histogram, Service::kStateConfiguring,
                                 Service::kStateConnected);
  histogram =
      GetFullMetricName(kMetricTimeToPortalMillisecondsSuffix, technology);
  AddServiceStateTransitionTimer(service, histogram, Service::kStateConnected,
                                 Service::kStateNoConnectivity);
  histogram = GetFullMetricName(kMetricTimeToRedirectFoundMillisecondsSuffix,
                                technology);
  AddServiceStateTransitionTimer(service, histogram, Service::kStateConnected,
                                 Service::kStateRedirectFound);
  histogram =
      GetFullMetricName(kMetricTimeToOnlineMillisecondsSuffix, technology);
  AddServiceStateTransitionTimer(service, histogram, Service::kStateConnected,
                                 Service::kStateOnline);
}

void Metrics::UpdateServiceStateTransitionMetrics(
    ServiceMetrics* service_metrics, Service::ConnectState new_state) {
  const char* state_string = Service::ConnectStateToString(new_state);
  SLOG(this, 5) << __func__ << ": new_state=" << state_string;
  TimerReportersList& start_timers = service_metrics->start_on_state[new_state];
  for (auto& start_timer : start_timers) {
    SLOG(this, 5) << "Starting timer for " << start_timer->histogram_name()
                  << " due to new state " << state_string << ".";
    start_timer->Start();
  }

  TimerReportersList& stop_timers = service_metrics->stop_on_state[new_state];
  for (auto& stop_timer : stop_timers) {
    SLOG(this, 5) << "Stopping timer for " << stop_timer->histogram_name()
                  << " due to new state " << state_string << ".";
    if (stop_timer->Stop())
      stop_timer->ReportMilliseconds();
  }
}

void Metrics::SendServiceFailure(const Service& service) {
  NetworkServiceError error = kNetworkServiceErrorUnknown;
  // Explicitly map all possible failures. So when new failures are added,
  // they will need to be mapped as well. Otherwise, the compiler will
  // complain.
  switch (service.failure()) {
    case Service::kFailureNone:
      error = kNetworkServiceErrorNone;
      break;
    case Service::kFailureAAA:
      error = kNetworkServiceErrorAAA;
      break;
    case Service::kFailureActivation:
      error = kNetworkServiceErrorActivation;
      break;
    case Service::kFailureBadPassphrase:
      error = kNetworkServiceErrorBadPassphrase;
      break;
    case Service::kFailureBadWEPKey:
      error = kNetworkServiceErrorBadWEPKey;
      break;
    case Service::kFailureConnect:
      error = kNetworkServiceErrorConnect;
      break;
    case Service::kFailureDHCP:
      error = kNetworkServiceErrorDHCP;
      break;
    case Service::kFailureDNSLookup:
      error = kNetworkServiceErrorDNSLookup;
      break;
    case Service::kFailureEAPAuthentication:
      error = kNetworkServiceErrorEAPAuthentication;
      break;
    case Service::kFailureEAPLocalTLS:
      error = kNetworkServiceErrorEAPLocalTLS;
      break;
    case Service::kFailureEAPRemoteTLS:
      error = kNetworkServiceErrorEAPRemoteTLS;
      break;
    case Service::kFailureHTTPGet:
      error = kNetworkServiceErrorHTTPGet;
      break;
    case Service::kFailureIPSecCertAuth:
      error = kNetworkServiceErrorIPSecCertAuth;
      break;
    case Service::kFailureIPSecPSKAuth:
      error = kNetworkServiceErrorIPSecPSKAuth;
      break;
    case Service::kFailureInternal:
      error = kNetworkServiceErrorInternal;
      break;
    case Service::kFailureNeedEVDO:
      error = kNetworkServiceErrorNeedEVDO;
      break;
    case Service::kFailureNeedHomeNetwork:
      error = kNetworkServiceErrorNeedHomeNetwork;
      break;
    case Service::kFailureNotAssociated:
      error = kNetworkServiceErrorNotAssociated;
      break;
    case Service::kFailureNotAuthenticated:
      error = kNetworkServiceErrorNotAuthenticated;
      break;
    case Service::kFailureOTASP:
      error = kNetworkServiceErrorOTASP;
      break;
    case Service::kFailureOutOfRange:
      error = kNetworkServiceErrorOutOfRange;
      break;
    case Service::kFailurePPPAuth:
      error = kNetworkServiceErrorPPPAuth;
      break;
    case Service::kFailurePinMissing:
      error = kNetworkServiceErrorPinMissing;
      break;
    case Service::kFailureTooManySTAs:
      error = kNetworkServiceErrorTooManySTAs;
      break;
    case Service::kFailureDisconnect:
      error = kNetworkServiceErrorDisconnect;
      break;
    case Service::kFailureUnknown:
    case Service::kFailureMax:
      error = kNetworkServiceErrorUnknown;
      break;
  }

  string histogram =
      GetFullMetricName(kMetricNetworkServiceErrorSuffix, service.technology());

  // Publish technology specific connection failure metrics. This will
  // account for all the connection failures happening while connected to
  // a particular interface e.g. wifi, cellular etc.
  library_->SendEnumToUMA(histogram, error, kNetworkServiceErrorMax);

  // This is a generic Network service failure metrics agnostic to the
  // underlying interface. This metrics will account for all network
  // failures.
  library_->SendEnumToUMA(kMetricNetworkServiceErrors, error,
                          kNetworkServiceErrorMax);
}

Metrics::DeviceMetrics* Metrics::GetDeviceMetrics(int interface_index) const {
  DeviceMetricsLookupMap::const_iterator it =
      devices_metrics_.find(interface_index);
  if (it == devices_metrics_.end()) {
    SLOG(this, 2) << __func__ << ": device " << interface_index << " not found";
    return nullptr;
  }
  return it->second.get();
}

bool Metrics::IsTechnologyPresent(Technology technology_id) const {
  for (const auto& metrics : devices_metrics_) {
    if (metrics.second->technology == technology_id)
      return true;
  }
  return false;
}

void Metrics::AutoConnectMetricsReset(DeviceMetrics* device_metrics) {
  device_metrics->auto_connect_tries = 0;
  device_metrics->auto_connect_timer->Reset();
}

void Metrics::set_library(MetricsLibraryInterface* library) {
  chromeos_metrics::TimerReporter::set_metrics_lib(library);
  library_ = library;
}

}  // namespace shill
