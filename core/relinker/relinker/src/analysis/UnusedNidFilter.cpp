#include <relinker/analysis/UnusedNidFilter.hpp>
#include <relinker/analysis/UnusedNidFilter/IEntryPointCollector.hpp>
#include <relinker/analysis/UnusedNidFilter/IControlFlowGraph.hpp>
#include <relinker/analysis/UnusedNidFilter/IGotAccessIndex.hpp>
#include <relinker/analysis/UnusedNidFilter/IRelativeRelocationIndex.hpp>
#include <exception>
#include <iostream>

namespace Relinker {

class CfgBackedNidFilter : public IUnusedNidFilter {
public:
    std::vector<NidReference> Filter(
        const std::vector<NidReference>& nidRefs,
        const std::vector<std::uint8_t>& elfBytes,
        const std::vector<std::uint8_t>& textSection,
        VirtualAddress textVAddr
    ) override {
        if (textSection.empty()) return nidRefs;

        try {
            auto collector = UnusedNidFilter::MakeEntryPointCollector();
            auto entries = collector->Collect(elfBytes, textVAddr, textSection.size());

            if (entries.empty()) {
                std::cerr << "WARNING: CFG NID filtering skipped: no entry points found; keeping all NID references\n";
                return nidRefs;
            }

            VirtualAddress primary = entries[0];
            std::vector<VirtualAddress> extra(entries.begin() + 1, entries.end());

            auto relativeRelocations = UnusedNidFilter::BuildRelativeRelocationIndex(elfBytes);
            auto cfg = UnusedNidFilter::BuildControlFlowGraph(textSection, textVAddr, primary, extra, *relativeRelocations);
            auto index = UnusedNidFilter::BuildGotAccessIndex(*cfg, textSection, textVAddr);

            std::vector<NidReference> result;
            for (const auto& ref : nidRefs)
                if (index->IsGotSlotAccessed(ref.RelocationAddress))
                    result.push_back(ref);
            return result;
        } catch (const std::exception& e) {
            std::cerr << "WARNING: CFG NID filtering failed (" << e.what() << "); keeping all NID references\n";
            return nidRefs;
        }
    }
};

std::shared_ptr<IUnusedNidFilter> MakeUnusedNidFilter() {
    return std::make_shared<CfgBackedNidFilter>();
}

}
