// Attribute store. Mirrors CAttribute / CAttributeStore in BasePaint.dll
// (REA ev_4c7ccd8f...: Add, Get, GetRange, GetValue, SetValue,
// LoadProfileSettings, SaveProfileSettings). Profile persistence here is an
// in-memory map; the Win32 stage can feed registry values into it.
#pragma once

#include <map>
#include <memory>
#include <string>

#include "color.h"

namespace pr {

class Attribute {
public:
    enum class Type { Int, Double, String, Color };

    explicit Attribute(int value) : type_(Type::Int), int_(value) {}
    explicit Attribute(double value) : type_(Type::Double), double_(value) {}
    explicit Attribute(const std::string& value) : type_(Type::String), string_(value) {}
    explicit Attribute(Color value) : type_(Type::Color), color_(value) {}

    Type type() const { return type_; }
    bool GetValue(int& out) const {
        if (type_ != Type::Int) return false;
        out = int_;
        return true;
    }
    bool GetValue(double& out) const {
        if (type_ != Type::Double) return false;
        out = double_;
        return true;
    }
    bool GetValue(std::string& out) const {
        if (type_ != Type::String) return false;
        out = string_;
        return true;
    }
    bool GetValue(Color& out) const {
        if (type_ != Type::Color) return false;
        out = color_;
        return true;
    }
    bool SetValue(int value) {
        if (type_ != Type::Int) return false;
        int_ = value;
        return true;
    }
    bool SetValue(double value) {
        if (type_ != Type::Double) return false;
        double_ = value;
        return true;
    }
    bool SetValue(const std::string& value) {
        if (type_ != Type::String) return false;
        string_ = value;
        return true;
    }
    bool SetValue(Color value) {
        if (type_ != Type::Color) return false;
        color_ = value;
        return true;
    }

private:
    Type type_;
    int int_ = 0;
    double double_ = 0.0;
    std::string string_;
    Color color_;
};

class CAttributeStore {
public:
    void Add(int id, std::unique_ptr<Attribute> attribute) {
        attributes_[id] = std::move(attribute);
    }
    void Add(int id, int value) { Add(id, std::make_unique<Attribute>(value)); }
    void Add(int id, double value) { Add(id, std::make_unique<Attribute>(value)); }
    void Add(int id, const std::string& value) {
        Add(id, std::make_unique<Attribute>(value));
    }
    void Add(int id, Color value) { Add(id, std::make_unique<Attribute>(value)); }

    std::shared_ptr<Attribute> Get(int id) const {
        const auto it = attributes_.find(id);
        return it == attributes_.end() ? nullptr : it->second;
    }
    bool GetRange(int id, double& min, double& max) const {
        const auto it = ranges_.find(id);
        if (it == ranges_.end()) return false;
        min = it->second.first;
        max = it->second.second;
        return true;
    }
    void Add(int id, double min, double max) { ranges_[id] = {min, max}; }

    template <typename T>
    bool GetValue(int id, T& out) const {
        const auto a = Get(id);
        return a && a->GetValue(out);
    }
    template <typename T>
    bool SetValue(int id, const T& value) {
        const auto a = Get(id);
        return a && a->SetValue(value);
    }

    // In-memory stand-ins for LoadProfileSettings / SaveProfileSettings.
    void LoadProfileSettings(const std::map<int, std::shared_ptr<Attribute>>& profile) {
        for (const auto& kv : profile) attributes_[kv.first] = kv.second;
    }
    std::map<int, std::shared_ptr<Attribute>> SaveProfileSettings() const {
        return attributes_;
    }

private:
    std::map<int, std::shared_ptr<Attribute>> attributes_;
    std::map<int, std::pair<double, double>> ranges_;
};

}  // namespace pr
