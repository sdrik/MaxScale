/*
 * Copyright (c) 2018 MariaDB Corporation Ab
 *
 * Use of this software is governed by the Business Source License included
 * in the LICENSE.TXT file and at www.mariadb.com/bsl11.
 *
 * Change Date: 2024-03-10
 *
 * On the date above, in accordance with the Business Source License, use
 * of this software will be governed by version 2 or later of the General
 * Public License.
 */
#pragma once

#include <maxbase/xml.hh>
#include <memory>
#include <sstream>
#include <stdexcept>

// This is likely to be included in <maxbase/xml.hh>, hence here also
// placed in the maxbase::xml namespace.

namespace maxbase
{

namespace xml
{

class Exception : public std::runtime_error
{
    using std::runtime_error::runtime_error;
};

/**
 * Get descendant by path.
 *
 * @param ancestor  The ancestor to be used as root.
 * @param zPath     The path to a descendant.
 *
 * @return The descendant.
 *
 * @throws @c Exception if path does not refer to an element.
 */
xmlNode& get_descendant(xmlNode& ancestor,
                        const char* zPath);

/**
 * Find descendant node corresponding to particular xpath.
 *
 * @param ancestor  The node to use as root when searching.
 * @param zXpath    The xpath, defined relative to @c ancestor. Before
 *                  used, the xpath will be prepended with "./".
 *
 * @return The descendant corresponding to the xpath.
 *
 * @throws @c Exception if the number of found elements is something else but 1.
 */
xmlNode& get_descendant_by_xpath(xmlNode& ancestor, const char* zXpath);

/**
 * @brief Return XML content as a specific type.
 *
 * @param pContent  Content as returned by xmlNodeGetContent().
 *
 * @return The textual content converted to the specific type.
 *
 * @throws @c Exception if the content cannot be converted to the type.
 */
template<class T>
T get_content_as(const xmlChar* pContent);

template<>
long get_content_as<long>(const xmlChar* zContent);

template<>
inline const char* get_content_as<const char*>(const xmlChar* pContent)
{
    return reinterpret_cast<const char*>(pContent);
}

template<>
inline std::string get_content_as<std::string>(const xmlChar* pContent)
{
    return get_content_as<const char*>(pContent);
}

/**
 * @brief Return XML content as a specific type.
 *
 * @param node  The node whose content should be returned as a specific type.
 *
 * @return The textual node content converted to the specific type.
 *
 * @throws @c Exception if the content cannot be converted to the type.
 */
template<class T>
T get_content_as(xmlNode& node)
{
    std::unique_ptr<xmlChar> sContent(xmlNodeGetContent(&node));
    return mxb::xml::get_content_as<T>(sContent.get());
}

/**
 * @brief Return XML content as a specific type.
 *
 * @param ancestor  The node to be used as root.
 * @param zPath     The path of a descendant node.
 *
 * @return The textual node content converted to the specific type.
 *
 * @throws @c Exception if the node does not exist or if the content cannot
 *         be converted to the type.
 */
template<class T>
T get_content_as(xmlNode& ancestor, const char* zPath)
{
    xmlNode& node = get_descendant(ancestor, zPath);
    return get_content_as<T>(node);
}

/**
 * @brief Set the content of a node.
 *
 * @param node  The node whose content should be set.
 * @param t     The content.
 */
template<class T>
void set_content(xmlNode& node, const T& t)
{
    std::ostringstream ss;
    ss << t;

    xmlNodeSetContent(&node, reinterpret_cast<const xmlChar*>(ss.str().c_str()));
}

/**
 * @brief Set the content of a node.
 *
 * @param ancestor  The ancestor to be used as root.
 * @param zPath     The path to a descendant.
 * @param t         The content.
 *
 * @throws @c Exception if the path does not refer to a node.
 */
template<class T>
void set_content(xmlNode& ancestor, const char* zPath, const T& t)
{
    xmlNode& node = get_descendant(ancestor, zPath);
    set_content(node, t);
}

}
}
