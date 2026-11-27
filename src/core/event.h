// Listener contract, modelled on IPaintListener / CEventPublisher / CEvent
// observed in BasePaint.dll (REA ev_4c7ccd8f...).
#pragma once

namespace pr {

struct Event {
    enum class Kind { ImageChanged, SelectionChanged, ToolChanged, AttributeChanged };
    Kind kind = Kind::ImageChanged;
};

class IPaintListener {
public:
    virtual ~IPaintListener() = default;
    virtual void OnNotify(const Event& event) = 0;
};

}  // namespace pr
