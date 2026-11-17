// Copyright 2026 Zitter contributors. SPDX-License-Identifier: Apache-2.0
#ifndef ZITTER_CHROMIUM_PROTOTYPE_SITE_STORAGE_H_
#define ZITTER_CHROMIUM_PROTOTYPE_SITE_STORAGE_H_

#include <string>

#include "net/base/registry_controlled_domains/registry_controlled_domain.h"
#include "url/origin.h"

namespace zitter::prototype {

// The site a page's data belongs to: its registrable domain (wp.pl for
// wiadomosci.wp.pl), or the whole host for IP addresses and names like
// localhost. Clearing a site's data clears this domain with all its
// subdomains: clearing a single origin is not enough, because sites mirror
// their state through a hidden frame on a sibling subdomain (wp.pl keeps its
// consent in www.wp.pl's local storage) and write it back into cookies on the
// next visit.
inline std::string SiteDataDomain(const url::Origin& origin) {
  auto domain = net::registry_controlled_domains::GetDomainAndRegistry(
      origin, net::registry_controlled_domains::INCLUDE_PRIVATE_REGISTRIES);
  return domain.empty() ? origin.host() : domain;
}

}  // namespace zitter::prototype

#endif  // ZITTER_CHROMIUM_PROTOTYPE_SITE_STORAGE_H_
