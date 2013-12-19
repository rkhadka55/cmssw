// -*- C++ -*-
//
// Package:     FWCore/Framework
// Class  :     SharedResourcesRegistry
// 
// Implementation:
//     [Notes on implementation]
//
// Original Author:  Chris Jones
//         Created:  Sun, 06 Oct 2013 15:48:50 GMT
//

// system include files
#include <algorithm>
#include <cassert>

// user include files
#include "SharedResourcesRegistry.h"
#include "FWCore/Framework/interface/SharedResourcesAcquirer.h"

namespace edm {

  const std::string SharedResourcesRegistry::kLegacyModuleResourceName{"__legacy__"};
  
  SharedResourcesRegistry*
  SharedResourcesRegistry::instance() {
    static SharedResourcesRegistry s_instance;
    return &s_instance;
  }
  
  void
  SharedResourcesRegistry::registerSharedResource(const std::string& resourceName){

    auto& mutexAndCounter = resourceMap_[resourceName];

    if(resourceName == kLegacyModuleResourceName) {
      for(auto & resource : resourceMap_) {
        if(!resource.second.first) {
          resource.second.first.reset(new std::recursive_mutex);
        }
        ++resource.second.second;
      }
    } else {
      if(mutexAndCounter.second == 0) {
        for(auto & resource : resourceMap_) {
          if(resource.first == kLegacyModuleResourceName) {
            mutexAndCounter.first.reset(new std::recursive_mutex);
            mutexAndCounter.second += resource.second.second;
            break;
          }
        }
      } else if(mutexAndCounter.second == 1 && !mutexAndCounter.first) {
        // make the resource if more than 1 module wants it
        mutexAndCounter.first = std::shared_ptr<std::recursive_mutex>( new std::recursive_mutex );
      }
      ++mutexAndCounter.second;
    }
  }

  SharedResourcesAcquirer
  SharedResourcesRegistry::createAcquirerForSourceDelayedReader() {
    if(not resourceForDelayedReader_) {
      resourceForDelayedReader_.reset(new std::recursive_mutex{});
    }
    std::vector<std::recursive_mutex*> mutexes = {resourceForDelayedReader_.get()};

    return SharedResourcesAcquirer(std::move(mutexes));
  }

  SharedResourcesAcquirer
  SharedResourcesRegistry::createAcquirer(std::vector<std::string> const& resourceNames) const {

    // The acquirer will acquire the shared resources declared by a module
    // so that only it can use those resources while it runs. The other
    // modules using the same resource will not be run until the module
    // that acquired the resources completes its task.

    // The legacy shared resource is special.
    // Legacy modules cannot run concurrently with each other or
    // any other module that has declared any shared resource. Treat
    // one modules that call usesResource with no argument in the
    // same way.

    // Sort by how often used and then by name
    // Consistent sorting avoids deadlocks and this particular order optimizes performance
    std::map<std::pair<unsigned int, std::string>, std::recursive_mutex*> sortedResources;

    // Is this acquirer for a module that depends on the legacy shared resource?
    if(std::find(resourceNames.begin(), resourceNames.end(), kLegacyModuleResourceName) != resourceNames.end()) {

      for(auto const& resource : resourceMap_) {
        // It's redundant to declare legacy if the legacy modules
        // all declare all the other resources, so just skip it.
        // But if the only shared resource is the legacy resource don't skip it.
        if(resource.first == kLegacyModuleResourceName && resourceMap_.size() > 1) continue;
        //If only one module wants it, it really isn't shared
        if(resource.second.second > 1) {
          sortedResources.insert(std::make_pair(std::make_pair(resource.second.second, resource.first),resource.second.first.get()));
        }
      }
    // Handle cases where the module does not declare the legacy resource
    } else {
      for(auto const& name : resourceNames) {
        auto resource = resourceMap_.find(name);
        assert(resource != resourceMap_.end());
        //If only one module wants it, it really isn't shared
        if(resource->second.second > 1) {
          sortedResources.insert(std::make_pair(std::make_pair(resource->second.second, resource->first),resource->second.first.get()));
        }
      }
    }

    std::vector<std::recursive_mutex*> mutexes;
    mutexes.reserve(sortedResources.size());
    for(auto const& resource: sortedResources) {
      mutexes.push_back(resource.second);
    }
    return SharedResourcesAcquirer(std::move(mutexes));
  }
}
