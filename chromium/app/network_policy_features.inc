// Name, definition path, C++ symbol. tools/check_network_policy.py validates
// these against the pinned checkout so renamed flags cannot silently pass.
ZITTER_NETWORK_FEATURE("AimEnabled", "components/omnibox/browser/aim_eligibility_service_features.cc", kAimEnabled)
ZITTER_NETWORK_FEATURE("NetworkTimeServiceQuerying", "components/network_time/network_time_tracker.cc", kNetworkTimeServiceQuerying)
ZITTER_NETWORK_FEATURE("AutofillServerCommunication", "components/autofill/core/common/autofill_debug_features.cc", kAutofillServerCommunication)
ZITTER_NETWORK_FEATURE("TranslateRankerQuery", "components/translate/core/browser/translate_ranker_impl.cc", kTranslateRankerQuery)
ZITTER_NETWORK_FEATURE("TranslateRankerEnforcement", "components/translate/core/browser/translate_ranker_impl.cc", kTranslateRankerEnforcement)
ZITTER_NETWORK_FEATURE("OptimizationHints", "components/optimization_guide/core/optimization_guide_features.cc", kOptimizationHints)
ZITTER_NETWORK_FEATURE("OptimizationTargetPrediction", "components/optimization_guide/core/optimization_guide_features.cc", kOptimizationTargetPrediction)
ZITTER_NETWORK_FEATURE("OptimizationGuideModelExecution", "components/optimization_guide/core/optimization_guide_features.cc", kOptimizationGuideModelExecution)
ZITTER_NETWORK_FEATURE("MetricsReporting", "chrome/browser/metrics/chrome_metrics_services_manager_client.cc", kMetricsReportingFeature)
ZITTER_NETWORK_FEATURE("PreemptiveSodaDownload", "media/base/media_switches.cc", kPreemptiveSodaDownload)
