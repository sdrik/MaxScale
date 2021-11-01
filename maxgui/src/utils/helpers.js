/*
 * Copyright (c) 2020 MariaDB Corporation Ab
 *
 * Use of this software is governed by the Business Source License included
 * in the LICENSE.TXT file and at www.mariadb.com/bsl11.
 *
 * Change Date: 2025-10-11
 *
 * On the date above, in accordance with the Business Source License, use
 * of this software will be governed by version 2 or later of the General
 * Public License.
 */
import Vue from 'vue'
import update from 'immutability-helper'
import { v1 as uuidv1 } from 'uuid'
import deepDiff from 'deep-diff'
import { format as sqlFormatter } from './sqlFormatter'
export const isEmpty = require('lodash/isEmpty')
export const cloneDeep = require('lodash/cloneDeep')
export const isEqual = require('lodash/isEqual')
export const xorWith = require('lodash/xorWith')
export const uniqueId = require('lodash/uniqueId')
export const get = require('lodash/get')
export const unionBy = require('lodash/unionBy')
export const pick = require('lodash/pick')
export const pickBy = require('lodash/pickBy')
export const uniqBy = require('lodash/uniqBy')
export const merge = require('lodash/merge')
export const differenceWith = require('lodash/differenceWith')
export const countBy = require('lodash/countBy')
export const lodash = {
    isEmpty,
    cloneDeep,
    isEqual,
    xorWith,
    uniqueId,
    objectGet: get,
    unionBy,
    pick,
    pickBy,
    uniqBy,
    deepMerge: merge,
    differenceWith,
    countBy,
}

export function isNull(v) {
    return v === null
}

export function isUndefined(v) {
    return v === undefined
}

export function isFunction(v) {
    return typeof v === 'function'
}

export function isNotEmptyObj(v) {
    return !isNull(v) && !Array.isArray(v) && typeof v === 'object' && !isEmpty(v)
}
export function isNotEmptyArray(v) {
    return !isNull(v) && Array.isArray(v) && v.length > 0
}

export function getCookie(name) {
    let value = '; ' + document.cookie
    let parts = value.split('; ' + name + '=')
    if (parts.length == 2)
        return parts
            .pop()
            .split(';')
            .shift()
}

export function deleteCookie(name) {
    document.cookie = name + '=; expires=Thu, 01 Jan 1970 00:00:01 GMT;'
}
export function deleteAllCookies() {
    let cookies = document.cookie.split(';')
    for (const cookie of cookies) {
        deleteCookie(cookie)
    }
}

export function range(start, end) {
    if (isNaN(start) || isNaN(end)) return
    return Math.floor(Math.random() * (end - start + 1)) + start
}

//------------------------- Helper functions to display icon -------------------------------
export function serviceStateIcon(serviceState) {
    if (serviceState) {
        if (serviceState.includes('Started')) return 1
        if (serviceState.includes('Stopped')) return 2
        if (serviceState.includes('Allocated') || serviceState.includes('Failed')) return 0
        else return ''
    } else return ''
}
export function serverStateIcon(serverState) {
    let result = 2 // warning icon, warning text
    if (serverState) {
        // error icon, unhealthy text
        if (serverState === 'Running' || serverState.includes('Down')) result = 0
        // healthy icon, healthy text
        else if (serverState.includes('Running')) result = 1
        // warning icon
        if (serverState.includes('Maintenance')) result = 2
    }
    return result
}
export function repStateIcon(state) {
    if (state) {
        // error icon
        if (state === 'Stopped') return 0
        // healthy icon
        else if (state === 'Running') return 1
        // warning icon
        else return 2
    }
}
export function monitorStateIcon(monitorState) {
    if (monitorState) {
        if (monitorState.includes('Running')) return 1
        if (monitorState.includes('Stopped')) return 2
        else return ''
    } else return ''
}
export function listenerStateIcon(state) {
    if (state) {
        if (state === 'Running') return 1
        else if (state === 'Stopped') return 2
        else if (state === 'Failed') return 0
        else return ''
    } else return ''
}
export function delay(t, v) {
    return new Promise(function(resolve) {
        setTimeout(resolve.bind(null, v), t)
    })
}

export function dynamicColors(dataIndex) {
    const palette = [
        'rgba(171,199,74,1)',
        'rgba(245,157,52,1)',
        'rgba(47,153,163,1)',
        'rgba(150,221,207,1)',
        'rgba(125,208,18,1)',
        'rgba(14,100,136,1)',
        'rgba(66,79,98,1)',
        'rgba(0,53,69,1)',
        'rgba(45,156,219,1)',
    ]
    return palette[dataIndex % palette.length]
}

/**
 * This function replaces a char in payload.str at payload.index with payload.newChar
 * @param {String} payload.str - string to be processed
 * @param {Number} payload.index - index of char that will be replaced
 * @param {String} payload.newChar - new char
 * @returns new string
 */
export function strReplaceAt({ str, index, newChar }) {
    if (index > str.length - 1) return str
    return str.substr(0, index) + newChar + str.substr(index + 1)
}

/**
 * @param {Object|String} error - Error object or string that returns from try catch
 * @return {Array} An array of error string
 */
export function getErrorsArr(error) {
    let errorsArr = [error]

    if (!isUndefined(error.response) && !isUndefined(error.response.data.errors)) {
        errorsArr = error.response.data.errors.map(ele => `${ele.detail}`)
    }

    return errorsArr
}

/**
 * This function takes array of objects and object path to create a hash map
 * using provided argument path. Key value will be always an array of objects.
 * Meaning that if there are duplicated key values at provided argument path, the value will be
 * pushed to the array. This makes this function different compare to built in Map object
 * @param {Array} payload.arr - Array of objects to be hashed by provided keyName
 * @param {String} payload.path - path of object to be hashed by. e.g. 'attributes.module_type' or 'parentNodeId'
 * @return {Object} hashMap
 */
export function hashMapByPath({ arr, path }) {
    let hashMap = {}
    arr.forEach(obj => {
        const keyValue = lodash.objectGet(obj, path)
        if (hashMap[keyValue] === undefined) hashMap[keyValue] = []
        hashMap[keyValue].push(obj)
    })
    return hashMap
}

/**
 * Handle format date value
 * @param {String} payload.value - String date to be formatted
 * @param {String} payload.formatType - format type (default is HH:mm:ss MM.DD.YYYY)
 * @return {String} new date format
 */
export function dateFormat({ value, formatType = 'HH:mm:ss MM.DD.YYYY' }) {
    let date = new Date(value)
    const DATE_RFC2822 = 'ddd, DD MMM YYYY HH:mm:ss'
    let format
    switch (formatType) {
        case 'DATE_RFC2822':
            format = DATE_RFC2822
            break
        default:
            format = formatType
    }
    return Vue.moment(date).format(format)
}

let nodeId = 0 // must be a number, so that hierarchySort can be done
/**
 * Convert an object to tree array.
 * Root id (parentNodeId) is always started at 0
 * @param {Object} payload.obj - Root object to be handled
 * @param {Boolean} payload.keepPrimitiveValue - keepPrimitiveValue to whether call convertType function or not
 * @param {Number} payload.level - depth level for nested object
 * @param {Number} payload.parentNodeId - nodeId of parentNode
 * @return {Array} an array of nodes object
 */
export function objToTree({ obj, keepPrimitiveValue, level, parentNodeId = 0 }) {
    let tree = []
    if (isNotEmptyObj(obj)) {
        const targetObj = cloneDeep(obj)
        Object.keys(targetObj).forEach(key => {
            const value = keepPrimitiveValue ? targetObj[key] : convertType(targetObj[key])

            let node = {
                nodeId: ++nodeId,
                parentNodeId,
                level,
                id: key,
                value: value,
                originalValue: value,
            }

            const hasChild = isNotEmptyArray(value) || isNotEmptyObj(value)
            node.leaf = !hasChild
            if (hasChild) {
                node.value = ''
                //  only object has child value will have expanded property
                node.expanded = false
            }

            if (isNotEmptyObj(value))
                node.children = objToTree({
                    obj: value,
                    keepPrimitiveValue,
                    level: level + 1,
                    parentNodeId: node.nodeId,
                })
            if (isNotEmptyArray(value))
                //convert value type array to object then do a recursive call
                node.children = objToTree({
                    obj: { ...value },
                    keepPrimitiveValue,
                    level: level + 1,
                    parentNodeId: node.nodeId,
                })

            tree.push(node)
        })
    }
    return tree
}

/**
 * This function flattens tree array
 * @param {Array} tree - tree array to be flatten
 * @returns {Array} flattened array
 */
export function flattenTree(tree) {
    let flattened = []
    let target = cloneDeep(tree)
    //Traversal
    target.forEach(o => {
        if (o.children && o.children.length > 0) {
            o.expanded = true
            flattened.push(o)
            flattened = [...flattened, ...flattenTree(o.children)]
        } else flattened.push(o)
    })
    return flattened
}

/**
 * This function finds the ancestor node id of provided argument node
 * @param {Number} payload.node - node to be used for finding its ancestor
 * @param {Map} payload.treeMap - map for find specific node using nodeId
 * @returns {Number} ancestor node id
 */
export function findAncestor({ node, treeMap }) {
    const { nodeId } = node
    let ancestors = []
    let parentId = treeMap.get(nodeId) && treeMap.get(nodeId).parentNodeId
    while (parentId) {
        ancestors.push(parentId)
        parentId = treeMap.get(parentId) && treeMap.get(parentId).parentNodeId
    }
    // since nodeId is an incremental number, the ancestor nodeId should be the smallest number
    if (ancestors.length) return Math.min(...ancestors)
    // root parentNodeId is always 0
    else return 0
}

/**
 * This function takes tree for creating a tree map to
 * lookup for changed nodes and finally return an object
 * with key pairs as changed nodes id and value. This object
 * respects depth level of nested objects.
 * e.g. If a changed nodes are [ { id: 'count', value: 10, ... } ]
 * The result object would be { log_throttling { window: 0, suppress: 0, count: 10 }}
 * @param {Array} payload.arr - Array of objects
 * @param {Array} payload.tree - tree
 * @return {Object} object
 */
export function treeToObj({ changedNodes, tree }) {
    let resultObj = {}
    if (isNotEmptyArray(changedNodes)) {
        let ancestorsHash = {}
        const target = cloneDeep(changedNodes)
        let treeMap = new Map()
        const flattened = flattenTree(tree)
        flattened.forEach(node => treeMap.set(node.nodeId, node))

        target.forEach(node => {
            const { parentNodeId } = node
            // if a node changes its value, its ancestor needs to be included in the resultObj
            if (parentNodeId) {
                const ancestorId = findAncestor({ node, treeMap })
                const ancestorNode = treeMap.get(ancestorId)
                if (ancestorNode) {
                    const { originalValue, id: ancestorId } = ancestorNode
                    ancestorsHash[ancestorId] = originalValue
                    updateNode({ obj: ancestorsHash[ancestorId], node: node })
                    resultObj[ancestorId] = ancestorsHash[ancestorId]
                }
            } else if (node.leaf) resultObj[node.id] = node.value
        })
    }
    return resultObj
}

/**
 *
 * This function mutates nested property of obj (ancestor object)
 * using id and value of node obj. The id of node obj
 * is the key of ancestor object at unknown level while the value is
 * the new value for that key.
 * @param {Object} payload.obj - ancestor object
 * @param {Object} payload.node - node that contains id and value.
 */
export function updateNode({ obj, node }) {
    const { id: key, value } = node
    if (obj[key] !== undefined) obj[key] = value
    else
        for (const prop in obj) {
            if (obj[prop] && typeof obj[prop] === 'object') updateNode({ obj: obj[prop], node })
        }
}

/**
 * This function converts type null and undefined to type string
 * with value as 'undefined' and 'null' respectively
 * @param {Any} value - Any types that needs to be handled
 * @return {Any} value
 */
export function convertType(value) {
    const typeOfValue = typeof value
    let newVal = value
    if (typeOfValue === 'undefined') {
        newVal = typeOfValue
    }
    // handle typeof null object and empty string
    if (value === null) newVal = 'null'
    return newVal
}

export function capitalizeFirstLetter(string) {
    return string.charAt(0).toUpperCase() + string.slice(1)
}

/**
 * This function converts to bits or bytes from provided
 * suffix argument when reverse argument is false, otherwise
 * it reverses the conversion from either bits or bytes to provided suffix argument
 * @param {String} payload.suffix - size suffix: Ki, Mi, Gi, Ti or k, M, G, T
 * @param {Number} payload.val - value to be converted
 * @param {Boolean} payload.isIEC - if it is true, it use 1024 for multiples of bytes (B), otherwise 1000 of bits
 * @param {Boolean} payload.reverse - should reverse convert or not
 * @returns {Number} new size value
 */
export function convertSize({ suffix, val, isIEC = false, reverse = false }) {
    let result = val
    let base
    let multiple = isIEC ? 1024 : 1000
    switch (suffix) {
        case 'Ki':
        case 'k':
            base = Math.pow(multiple, 1)
            break
        case 'Mi':
        case 'M':
            base = Math.pow(multiple, 2)
            break
        case 'Gi':
        case 'G':
            base = Math.pow(multiple, 3)
            break
        case 'Ti':
        case 'T':
            base = Math.pow(multiple, 4)
            break
        default:
            base = Math.pow(multiple, 0)
    }
    return reverse ? Math.floor(result / base) : result * base
}

/**
 * This function converts to milliseconds from provided suffix argument by default.
 * If toMilliseconds is false, it converts milliseconds value to provided suffix argument
 * @param {String} payload.suffix duration suffix: ms,s,m,h
 * @param {Number} payload.val value to be converted. Notice: should be ms value if toMilliseconds is false
 * @param {Boolean} payload.toMilliseconds whether to convert to milliseconds
 * @return {Number} returns converted duration value
 */
export function convertDuration({ suffix, val, toMilliseconds = true }) {
    let result
    switch (suffix) {
        case 's':
            result = toMilliseconds ? val * 1000 : val / 1000
            break
        case 'm':
            result = toMilliseconds ? val * 60 * 1000 : val / (60 * 1000)
            break
        case 'h':
            result = toMilliseconds ? val * 60 * 60 * 1000 : val / (60 * 60 * 1000)
            break
        case 'ms':
        default:
            result = val
    }
    return Math.floor(result)
}

/**
 * @param {Object} param - parameter object must contain string value property
 * @param {Array} suffixes - an array of suffixes name .e.g. ['ms', 's', 'm', 'h']
 * @return {Object} object info {suffix:suffix, indexOfSuffix: indexOfSuffix}
 * suffix as suffix name, indexOfSuffix as the begin index of that suffix in param.value
 */
export function getSuffixFromValue(param, suffixes) {
    let suffix = null
    let indexOfSuffix = null
    // get suffix from param.value string
    for (let i = 0; i < suffixes.length; ++i) {
        if (param.value.includes(suffixes[i])) {
            suffix = suffixes[i]
            indexOfSuffix = param.value.indexOf(suffix)
            break
        }
    }
    return { suffix: suffix, indexOfSuffix: indexOfSuffix }
}

/**
 * This function creates dataset object for line-chart-stream
 * @param {String} payload.label - label for dataset
 * @param {Number} payload.value - value for dataset
 * @param {Number} payload.colorIndex - index of color from color palette of dynamicColors helper
 * @param {Number} [payload.timestamp] - if provided, otherwise using Date.now() (optional)
 * @param {String|Number} [payload.id] - unique id (optional)
 * @param {Array} [payload.data] - data for dataset (optional)
 * @returns {Object} dataset object
 */
export function genLineStreamDataset({ label, value, colorIndex, timestamp, id, data }) {
    const lineColor = dynamicColors(colorIndex)
    const indexOfOpacity = lineColor.lastIndexOf(')') - 1
    const backgroundColor = strReplaceAt({ str: lineColor, index: indexOfOpacity, newChar: '0.1' })
    let time = Date.now()
    if (timestamp) time = timestamp
    let dataset = {
        label: label,
        id: label,
        type: 'line',
        // background of the line
        backgroundColor: backgroundColor,
        borderColor: lineColor,
        borderWidth: 1,
        lineTension: 0,
        data: [{ x: time, y: value }],
    }
    if (id) dataset.resourceId = id
    if (data) dataset.data = data
    return dataset
}

/**
 * The function is used to convert plural resource name to singular and capitalize
 * first letter for UI usage
 * @param {String} str string to be processed
 * @return {String} return str that removed last char s and capitalized first char
 */
export function resourceTxtTransform(str) {
    let lowerCaseStr = str.toLowerCase()
    const suffix = 's'
    const chars = lowerCaseStr.split('')
    if (chars[chars.length - 1] === suffix) {
        lowerCaseStr = strReplaceAt({
            str: lowerCaseStr,
            index: chars.length - 1,
            newChar: '',
        })
    }
    return capitalizeFirstLetter(lowerCaseStr)
}
/**
 * Case insensitive check if substring is included in source string
 * @param {String} str source string
 * @param {String} subStr sub string to be searched
 * @return {Boolean} Return Boolean
 */
export function ciStrIncludes(str, subStr) {
    return str.toLowerCase().includes(subStr.toLowerCase())
}

/**
 * Vue.nextTick is not enough for rendering large DOM.
 * This function uses double RAF technique to wait for a browser repaint
 * @param {Function} callback callback function
 */
export function doubleRAF(callback) {
    requestAnimationFrame(() => {
        requestAnimationFrame(callback)
    })
}

/**
 * @param {String} str plain identifier string. e.g. db_name.tbl_name
 * @return {String} Return escaped identifier string. e.g.  \`db_name\`.\`tbl_name\`
 */
export function escapeIdentifiers(str) {
    return str
        .split('.')
        .map(identifier => `\`${identifier}\``)
        .join('.')
}

/**
 * @param {Array} payload.columns table fields
 * @param {Array} payload.rows table rows
 * @return {Array} Return object rows
 */
export function getObjectRows({ columns, rows }) {
    return rows.map(row => {
        const obj = {}
        columns.forEach((c, index) => {
            obj[c] = row[index]
        })
        return obj
    })
}

export function pxToPct({ px, containerPx }) {
    return (px / containerPx) * 100
}

export const immutableUpdate = update

/**
 * @param {String|Number} value value to be handled
 * @returns {String} Returns px unit string
 */
export function handleAddPxUnit(value) {
    if (typeof value === 'number') return `${value}px`
    return value
}

/**
 * This function is not working on macOs as the scrollbar is only showed when scrolling.
 * However, on Macos, scrollbar is placed above the content (overlay) instead of taking up space
 * of the content. So in macOs, this returns 0.
 * @returns {Number} scrollbar width
 */
export function getScrollbarWidth() {
    // Creating invisible container
    const outer = document.createElement('div')
    outer.style.visibility = 'hidden'
    outer.style.overflow = 'scroll' // forcing scrollbar to appear
    outer.style.msOverflowStyle = 'scrollbar' // needed for WinJS apps
    document.body.appendChild(outer)

    // Creating inner element and placing it in the container
    const inner = document.createElement('div')
    outer.appendChild(inner)

    // Calculating difference between container's full width and the child width
    const scrollbarWidth = outer.offsetWidth - inner.offsetWidth

    // Removing temporary elements from the DOM
    outer.parentNode.removeChild(outer)

    return scrollbarWidth
}

/**
 * @private
 * @param {String} text
 */
function fallbackCopyTextToClipboard(text) {
    let txtArea = document.createElement('textarea')
    txtArea.value = text
    txtArea.style = { ...txtArea.style, top: 0, left: 0, position: 'fixed' }
    document.body.appendChild(txtArea)
    txtArea.focus()
    txtArea.select()
    document.execCommand('copy')
    document.body.removeChild(txtArea)
}

/**
 * @param {String} text
 */
export function copyTextToClipboard(text) {
    if (navigator.clipboard) {
        navigator.clipboard.writeText(text)
    } else fallbackCopyTextToClipboard(text)
}

/**
 * @private
 * This copies inherit styles from srcNode to dstNode
 * @param {Object} payload.srcNode - html node to be copied
 * @param {Object} payload.dstNode - target html node to pasted
 */
function copyNodeStyle({ srcNode, dstNode }) {
    const computedStyle = window.getComputedStyle(srcNode)
    Array.from(computedStyle).forEach(key =>
        dstNode.style.setProperty(
            key,
            computedStyle.getPropertyValue(key),
            computedStyle.getPropertyPriority(key)
        )
    )
}
export function removeTargetDragEle(dragTargetId) {
    let elem = document.getElementById(dragTargetId)
    if (elem) elem.parentNode.removeChild(elem)
}
export function addDragTargetEle({ e, dragTarget, dragTargetId }) {
    let cloneNode = dragTarget.cloneNode(true)
    cloneNode.setAttribute('id', dragTargetId)
    cloneNode.textContent = dragTarget.textContent
    copyNodeStyle({ srcNode: dragTarget, dstNode: cloneNode })
    cloneNode.style.position = 'absolute'
    cloneNode.style.top = e.clientY + 'px'
    cloneNode.style.left = e.clientX + 'px'
    cloneNode.style.zIndex = 9999
    document.getElementById('app').appendChild(cloneNode)
}

/**
 * This allows to enter minus or hyphen minus and numbers
 * @param {Event} e - input evt
 */
export function preventNonInteger(e) {
    if (!e.key.match(/^[-]?\d*$/g)) e.preventDefault()
}
/**
 * This allows user to enter only number
 * @param {Event} e - input evt
 */
export function preventNonNumericalVal(e) {
    if (!e.key.match(/^\d*$/g)) e.preventDefault()
}
/**
 * This adds number of days to current date
 * @param {Number} days - Number of days
 * @returns {String} - returns date
 */
export function addDaysToNow(days) {
    let curr = new Date()
    return curr.setDate(curr.getDate() + days)
}
/**
 * This returns number of days between target timestamp and current date
 * @param {String} timestamp - target unix timestamp
 * @returns {Number} - days diff
 */
export function daysDiff(timestamp) {
    const now = Vue.moment().startOf('day')
    const end = Vue.moment(timestamp).startOf('day')
    return end.diff(now, 'days')
}

//TODO: Remove this. Replace with deep-diff which gives more details about diff
/**
 * Deep diff between two objects
 * @param  {Object} base - Base object
 * @param  {Object} object - New object
 * @returns {Object} - Returns a new object that represents the diff
 */
export function objectDiff({ base, object }) {
    const changes = {}

    function walkObject(base, object, path = '') {
        for (const key of Object.keys(base)) {
            const currentPath = path === '' ? key : `${path}.${key}`
            if (object[key] === undefined) changes[currentPath] = '-'
        }

        for (const [key, value] of Object.entries(object)) {
            let currentPath
            if (Array.isArray(object)) currentPath = path + `[${key}]`
            else currentPath = path === '' ? key : `${path}.${key}`

            if (base[key] === undefined) changes[currentPath] = '+'
            else if (value !== base[key])
                if (typeof value === 'object' && typeof base[key] === 'object')
                    walkObject(base[key], value, currentPath)
                else changes[currentPath] = object[key]
        }
    }
    walkObject(base, object)
    return changes
}

//TODO: objects Re-order in array diff
/**
 * @param {Array} payload.base - initial base array
 * @param {Array} payload.newArr - new array
 * @param {String} payload.idField - key name of unique value in each object in array
 * @returns {Map} - returns  Map { unchanged: [{}], added: [{}], updated:[{}], removed:[{}] }
 */
export function arrOfObjsDiff({ base, newArr, idField }) {
    // stored ids of two arrays to get removed objects
    const baseIds = []
    const newArrIds = []
    const baseMap = new Map()
    base.forEach(o => {
        baseIds.push(o[idField])
        baseMap.set(o[idField], o)
    })

    const resultMap = new Map()
    resultMap.set('unchanged', [])
    resultMap.set('added', [])
    resultMap.set('removed', [])
    resultMap.set('updated', [])

    newArr.forEach(obj2 => {
        newArrIds.push(obj2[idField])
        const obj1 = baseMap.get(obj2[idField])
        if (!obj1) resultMap.set('added', [...resultMap.get('added'), obj2])
        else if (isEqual(obj1, obj2))
            resultMap.set('unchanged', [...resultMap.get('unchanged'), obj2])
        else {
            const diff = deepDiff(obj1, obj2)
            const objDiff = { oriObj: obj1, newObj: obj2, diff }
            resultMap.set('updated', [...resultMap.get('updated'), objDiff])
        }
    })
    const removedIds = baseIds.filter(id => !newArrIds.includes(id))
    const removed = removedIds.map(id => baseMap.get(id))
    resultMap.set('removed', removed)
    return resultMap
}

export function formatSQL(v) {
    return sqlFormatter(v, {
        indent: '   ',
        uppercase: true,
        linesBetweenQueries: 2,
    })
}

Object.defineProperties(Vue.prototype, {
    $help: {
        get() {
            return {
                getCookie,
                deleteCookie,
                deleteAllCookies,
                range,
                serviceStateIcon,
                serverStateIcon,
                repStateIcon,
                monitorStateIcon,
                listenerStateIcon,
                isNotEmptyObj,
                isNotEmptyArray,
                delay,
                dynamicColors,
                strReplaceAt,
                getErrorsArr,
                hashMapByPath,
                dateFormat,
                objToTree,
                flattenTree,
                findAncestor,
                updateNode,
                treeToObj,
                convertType,
                capitalizeFirstLetter,
                getSuffixFromValue,
                convertDuration,
                convertSize,
                genLineStreamDataset,
                isNull,
                isFunction,
                isUndefined,
                lodash,
                immutableUpdate,
                resourceTxtTransform,
                ciStrIncludes,
                doubleRAF,
                escapeIdentifiers,
                getObjectRows,
                pxToPct,
                handleAddPxUnit,
                getScrollbarWidth,
                copyTextToClipboard,
                removeTargetDragEle,
                addDragTargetEle,
                preventNonNumericalVal,
                preventNonInteger,
                addDaysToNow,
                daysDiff,
                objectDiff,
                uuidv1,
                arrOfObjsDiff,
                formatSQL,
            }
        },
    },
})
