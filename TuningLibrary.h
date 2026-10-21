#pragma once
// Tuna — bibliothèque d'accordages : usine + accordages perso + favoris.
// Les accordages perso et les favoris sont communs à toutes les instances du plugin
// (fichier XML dans le dossier de données utilisateur : %APPDATA%\Studio Wiga\Tuna sous Windows,
//  ~/Library/Application Support/Studio Wiga/Tuna sous macOS). À utiliser sur le thread message.
#include <juce_core/juce_core.h>
#include <juce_data_structures/juce_data_structures.h>
#include "Tunings.h"
#include <set>

class TuningLibrary
{
public:
    TuningLibrary() { load(); }

    // liste complète : usine puis perso
    int size() const { return (int) tuna::factoryTunings().size() + (int) user.size(); }
    const tuna::Tuning& get (int i) const
    {
        const int nf = (int) tuna::factoryTunings().size();
        i = juce::jlimit (0, size() - 1, i);
        return i < nf ? tuna::factoryTunings()[(size_t) i] : user[(size_t) (i - nf)];
    }
    bool isUser (int i) const { return i >= (int) tuna::factoryTunings().size(); }

    static juce::String keyOf (const tuna::Tuning& t)
    {
        return (t.user ? "user|" : "factory|") + juce::String::fromUTF8 (t.category.c_str()) + "|" + juce::String::fromUTF8 (t.name.c_str());
    }
    juce::String keyAt (int i) const { return keyOf (get (i)); }

    int indexOfKey (const juce::String& key) const
    {
        for (int i = 0; i < size(); ++i) if (keyAt (i) == key) return i;
        return -1;
    }

    // ---- favoris ----
    bool isFavourite (int i) const { return favourites.count (keyAt (i)) > 0; }
    void toggleFavourite (int i)
    {
        const auto k = keyAt (i);
        if (favourites.count (k)) favourites.erase (k); else favourites.insert (k);
        save();
    }
    std::vector<int> favouriteIndices() const
    {
        std::vector<int> v;
        for (int i = 0; i < size(); ++i) if (isFavourite (i)) v.push_back (i);
        return v;
    }

    // ---- accordages perso ----
    int addUser (const juce::String& name, const std::vector<int>& notes)
    {
        tuna::Tuning t { uniqueName (name).toStdString(), "PERSO", notes, true };
        user.push_back (t);
        save();
        return size() - 1;
    }
    void updateUser (int i, const juce::String& name, const std::vector<int>& notes)
    {
        if (! isUser (i)) return;
        auto& t = user[(size_t) (i - (int) tuna::factoryTunings().size())];
        const auto oldKey = keyOf (t);
        if (juce::String::fromUTF8 (t.name.c_str()) != name) t.name = uniqueName (name).toStdString();
        t.notes = notes;
        if (favourites.count (oldKey)) { favourites.erase (oldKey); favourites.insert (keyOf (t)); }
        save();
    }
    void removeUser (int i)
    {
        if (! isUser (i)) return;
        favourites.erase (keyAt (i));
        user.erase (user.begin() + (i - (int) tuna::factoryTunings().size()));
        save();
    }

    void reload() { user.clear(); favourites.clear(); load(); }

    static juce::File file()
    {
        return juce::File::getSpecialLocation (juce::File::userApplicationDataDirectory)
                   .getChildFile ("Studio Wiga").getChildFile ("Tuna").getChildFile ("library.xml");
    }

private:
    juce::String uniqueName (juce::String n) const
    {
        n = n.trim();
        if (n.isEmpty()) n = "Accordage perso";
        juce::String candidate = n;
        for (int k = 2;; ++k)
        {
            bool taken = false;
            for (auto& t : user) if (juce::String::fromUTF8 (t.name.c_str()) == candidate) { taken = true; break; }
            if (! taken) return candidate;
            candidate = n + " " + juce::String (k);
        }
    }

    void load()
    {
        auto xml = juce::XmlDocument::parse (file());
        if (xml == nullptr) return;
        for (auto* e : xml->getChildWithTagNameIterator ("tuning"))
        {
            tuna::Tuning t;
            t.name = e->getStringAttribute ("name").toStdString();
            t.category = "PERSO";
            t.notes = tuna::parseNotes (e->getStringAttribute ("notes").toStdString());
            t.user = true;
            if (! t.notes.empty() && t.notes.size() <= 10) user.push_back (t);
        }
        for (auto* e : xml->getChildWithTagNameIterator ("favourite"))
            favourites.insert (e->getStringAttribute ("key"));
    }

    void save() const
    {
        juce::XmlElement root ("TunaLibrary");
        for (auto& t : user)
        {
            auto* e = root.createNewChildElement ("tuning");
            e->setAttribute ("name", juce::String::fromUTF8 (t.name.c_str()));
            e->setAttribute ("notes", juce::String (tuna::notesToString (t.notes)));
        }
        for (auto& k : favourites) root.createNewChildElement ("favourite")->setAttribute ("key", k);
        const auto f = file();
        f.getParentDirectory().createDirectory();
        root.writeTo (f);
    }

    std::vector<tuna::Tuning> user;
    std::set<juce::String> favourites;
};
