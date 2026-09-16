#pragma once

#include <QString>

struct GitStatusEntry
{
    QString indexStatus;
    QString workTreeStatus;
    QString path;
    QString originalPath;

    [[nodiscard]] bool isStaged() const
    {
        return indexStatus != " " && indexStatus != "?";
    }

    [[nodiscard]] bool isUnstaged() const
    {
        return workTreeStatus != " " || indexStatus == "?";
    }
};

