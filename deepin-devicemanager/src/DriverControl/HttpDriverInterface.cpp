// SPDX-FileCopyrightText: 2022 UnionTech Software Technology Co., Ltd.
//
// SPDX-License-Identifier: GPL-3.0-or-later

#include "HttpDriverInterface.h"
#include "commonfunction.h"
#include "commontools.h"
#include "DDLog.h"

#include <QJsonDocument>
#include <QtNetwork>
#include <QNetworkReply>
#include <QNetworkRequest>

#include <DSysInfo>

using namespace DDLog;

// 以下这个问题可以避免单例的内存泄露问题
std::atomic<HttpDriverInterface *> HttpDriverInterface::s_Instance;
std::mutex HttpDriverInterface::m_mutex;

HttpDriverInterface::HttpDriverInterface(QObject *parent) : QObject(parent)
{
    qCDebug(appLog) << "HttpDriverInterface constructor";
}

HttpDriverInterface::~HttpDriverInterface()
{
    qCDebug(appLog) << "HttpDriverInterface destructor";
}

QString HttpDriverInterface::getRequestJson(QString strUrl)
{
    qCDebug(appLog) << "Get request from URL:" << strUrl;
    strJsonDriverInfo = "";
    const QUrl newUrl = QUrl::fromUserInput(strUrl);

    QNetworkRequest request(newUrl);
    QNetworkAccessManager qnam;
    QNetworkReply *reply = qnam.get(request);

    QTimer timer;
    timer.setSingleShot(true);
    QEventLoop loop;
    QObject::connect(&timer, &QTimer::timeout, &loop, &QEventLoop::quit);
    QObject::connect(reply, &QNetworkReply::finished, &loop, &QEventLoop::quit);
    timer.start(10000);
    loop.exec();

    strJsonDriverInfo = reply->readAll();
    //! [networkreply-error-handling-1]
    QNetworkReply::NetworkError error = reply->error();

    reply->reset();
    reply->deleteLater();
    if (error != QNetworkReply::NoError) {
        qCInfo(appLog) << "strUrl : " << strUrl << "network error";
        return "network error";
    }
    qCInfo(appLog) << "strUrl : " << strUrl << strJsonDriverInfo;
    return strJsonDriverInfo;
}

void HttpDriverInterface::getRequest(DriverInfo *driverInfo)
{
    qCDebug(appLog) << "Get request for driver:" << driverInfo->m_Name << "Type:" << driverInfo->type();
    QString strJson;
    switch (driverInfo->type()) {
    case DR_Printer:
        strJson = getRequestPrinter(driverInfo->vendorName(), driverInfo->modelName()); break;
    //case DR_Camera:
    case DR_Scaner:
//        strJson = getRequestCamera(driverInfo->modelName());
//        break;
    case DR_Sound:
    case DR_Gpu:
    case DR_Network:
    case DR_OtherDevice:
    case DR_WiFi:
        strJson = getRequestBoard(driverInfo->vendorId(), driverInfo->modelId());
        break;
    default:
        qCDebug(appLog) << "Unsupported driver type for getRequest:" << driverInfo->type();
        break;
    }
    qCInfo(appLog) << "device name :" << driverInfo->m_Name  << "VendorId:" << driverInfo->m_VendorId << "ModelId:" << driverInfo->m_ModelId;
    if (strJson.contains("network error")) {
        qCWarning(appLog) << "Network error when getting driver info for:" << driverInfo->m_Name;
        emit sigRequestFinished(false, "network error");
    } else {
        checkDriverInfo(strJson, driverInfo);
        qCInfo(appLog) << "m_Packages:" << driverInfo->m_Packages;
        qCInfo(appLog) << "m_DebVersion:" << driverInfo->m_DebVersion;
        qCInfo(appLog) << "m_Status:" << driverInfo->m_Status;
    }
}

QString HttpDriverInterface::getRequestBoard(QString strManufacturer, QString strModels, int iClassP, int iClass)
{
    if(strManufacturer.isEmpty() || strModels.isEmpty()) {
        qCWarning(appLog) << "Empty manufacturer or model when getting board info";
        return QString();
    }
    qCDebug(appLog) << "getRequestBoard with manufacturer:" << strManufacturer << "models:" << strModels;
    QString arch = Common::getArchStore();
    QString build = getOsBuild();
    QString major, minor, strUrl;
    if (getVersion(major, minor) && major =="25") {
        strUrl = CommonTools::getUrl() + "?deb_manufacturer=" + strManufacturer;
        if (!strModels.isEmpty()) {
            strUrl += "&desc=" + strModels;
        }
        strUrl += "&arch=" + arch;
        if (!build.isEmpty()) {
            qCDebug(appLog) << "OS build is not empty, adding to URL";
            QString system = build;
            if (build[1] == "1") //专业版通过【产品线类型-产品线版本】方式进行系统构建匹配
                system = QString("%1-%2").arg(build[1]).arg(build[3]);
            strUrl += "&system=" + system;
        }
        strUrl += "&majorVersion=" + major;
        strUrl += "&minorVersion=" + minor;
    } else {
        strUrl = CommonTools::getUrl() + "?arch=" + arch;
        if (!build.isEmpty()) {
            qCDebug(appLog) << "OS build is not empty, adding to URL";
            QString system = build;

            if (build[1] == "1") //专业版通过【产品线类型-产品线版本】方式进行系统构建匹配
                system = QString("%1-%2").arg(build[1]).arg(build[3]);

            strUrl += "&system=" + system;
        }

        if (!strManufacturer.isEmpty()) {
            strUrl += "&deb_manufacturer=" + strManufacturer;
        }
        if (!strModels.isEmpty()) {
            strUrl += "&product=" + strModels;
        }
        if (0 < iClassP) {
            strUrl += "&class_p=" + QString::number(iClassP);
        }
        if (0 < iClass) {
            strUrl += "&class=" + QString::number(iClass);
        }
    }

    qCDebug(appLog) << "Constructed URL for getRequestBoard:" << strUrl;
    return getRequestJson(strUrl);
}

QString HttpDriverInterface::getRequestPrinter(QString strDebManufacturer, QString strDesc)
{
    qCDebug(appLog) << "getRequestPrinter with manufacturer:" << strDebManufacturer << "desc:" << strDesc;
    QString arch = Common::getArchStore();
    QString strUrl = CommonTools::getUrl() + "?arch=" + arch;
    int iType = DTK_CORE_NAMESPACE::DSysInfo::uosType();
    int iEditionType = DTK_CORE_NAMESPACE::DSysInfo::uosEditionType();
    strUrl += "&system=" + QString::number(iType) + '-' + QString::number(iEditionType);

    if (!strDebManufacturer.isEmpty()) {
        if (strDebManufacturer == "HP" || strDebManufacturer == "Hewlett-Packard") {
            qCDebug(appLog) << "Standardizing HP manufacturer name";
            strDebManufacturer = "HP";
        }
        strUrl += "&deb_manufacturer=" + strDebManufacturer;
    }
    if (!strDesc.isEmpty()) {
        strUrl += "&desc=" + strDesc;
    }
    qCDebug(appLog) << "Constructed URL for getRequestPrinter:" << strUrl;
    return getRequestJson(strUrl);
}

QString HttpDriverInterface::getRequestCamera(QString strDesc)
{
    qCDebug(appLog) << "getRequestCamera with desc:" << strDesc;
    QString arch = Common::getArchStore();
    QString strUrl = CommonTools::getUrl() + "?arch=" + arch;
    int iType = DTK_CORE_NAMESPACE::DSysInfo::uosType();
    int iEditionType = DTK_CORE_NAMESPACE::DSysInfo::uosEditionType();
    strUrl += "&system=" + QString::number(iType) + '-' + QString::number(iEditionType);

    if (!strDesc.isEmpty()) {
        strUrl += "&desc=" + strDesc;
    }
    qCDebug(appLog) << "Constructed URL for getRequestCamera:" << strUrl;
    return getRequestJson(strUrl);
}

void HttpDriverInterface::checkDriverInfo(QString strJson, DriverInfo *driverInfo)
{
    qCDebug(appLog) << "Checking driver info from JSON for driver:" << driverInfo->name();
    if (strJson.isEmpty()) {
        qCDebug(appLog) << "JSON string is empty, nothing to check.";
        return;
    }

    QList<RepoDriverInfo> lstDriverInfo;
    if (! convertJsonToDeviceList(strJson, lstDriverInfo)) {
        qCWarning(appLog) << "Failed to convert JSON to device list.";
        return;
    }

    if (lstDriverInfo.size() == 0) {
        qCDebug(appLog) << "No driver info found in JSON.";
        return;
    }

    // 找到最优等级
    int max = 0;
    foreach (const RepoDriverInfo &info, lstDriverInfo) {
        if (max < info.iLevel) {
            max = info.iLevel;
        }
    }

    // 找到最优选择
    int index = 0;
    int res_out = 0;
    for (int i = 0; i < lstDriverInfo.size(); i++) {
        if (max == lstDriverInfo[i].iLevel) {
            // 选中第一个最优等级的index
            int res = packageInstall(lstDriverInfo[i].strPackages, lstDriverInfo[i].strDebVersion);
            if (res > 0) {
                res_out = res;
                index = i;
            }
        }
        if (index == 0) {
            int res = packageInstall(lstDriverInfo[index].strPackages, lstDriverInfo[index].strDebVersion);
            if (res > 0) {
                res_out = res;
            }
        }
    }

    // 找到最优选择后，设置状态，最新、可安装、可更新
    driverInfo->m_DebVersion  = lstDriverInfo[index].strDebVersion;
    driverInfo->m_Packages = lstDriverInfo[index].strPackages;
    driverInfo->m_Size = lstDriverInfo[index].strSize;
    driverInfo->m_Byte = lstDriverInfo[index].bytes;

    if (2 == res_out) {
        // 此时说明最优版本已经安装
        driverInfo->m_Status = ST_DRIVER_IS_NEW;
    } else if (1 == res_out) {
        // 此时安装了最优包的不同版本
        driverInfo->m_Status = ST_CAN_UPDATE;
    } else {
        // 此时没有安装最优推荐包(包括其他版本)
        if (driverInfo->driverName().isEmpty() && driverInfo->type() != DR_Printer) {
            driverInfo->m_Status = ST_NOT_INSTALL;
        } else {
            // 此时安装了其他驱动
            driverInfo->m_Status = ST_CAN_UPDATE;
        }
    }
}

int HttpDriverInterface::packageInstall(const QString &package_name, const QString &version)
{
    qCDebug(appLog) << "Checking package installation status for:" << package_name << "version:" << version;
    // 0:没有包 1:版本不一致 2:版本一致
    QString outInfo = Common::executeClientCmd("apt", QStringList() << "policy" << package_name, QString(), -1, false);
    if (outInfo.isEmpty()) {
        qCDebug(appLog) << "No info from apt policy for package:" << package_name;
        return 0;
    }
    qCDebug(appLog) << "apt policy output:" << outInfo;
    QStringList infoList = outInfo.split("\n");
    int index = 0;
    for (int i = 0; i < infoList.size(); i++)
    {
        if (infoList[i].startsWith(package_name)) {
            index = i;
            qCDebug(appLog) << "Found package info at index:" << index;
            break;
        }
    }
    if (infoList.size() <= (2 + index) || infoList[1 + index].contains("（") || infoList[1 + index].contains("(")) {
        qCDebug(appLog) << "Installed version not found or in unexpected format.";
        return 0;
    }
    if (infoList[1 + index].contains(version)) {
        qCDebug(appLog) << "Installed version matches required version.";
        return 2;
    }

    QRegularExpression rxlen("(\\d+\\S*)");
    QRegularExpressionMatch match = rxlen.match(infoList[1 + index]);
    QString curVersion;
    if (match.hasMatch()) {
        curVersion = match.captured(1);
    }
    qCDebug(appLog) << "Current installed version:" << curVersion;
    // 若当前已安装版本高于推荐版本，不再更新
    if (curVersion >= version) {
        qCDebug(appLog) << "Current version is higher or equal to recommended, no update needed.";
        return 2;
    } else {
        qCDebug(appLog) << "Current version is lower than recommended, update needed.";
        return 1;
    }
}

QString HttpDriverInterface::getOsBuild()
{
    qCDebug(appLog) << "Get OS build info";
    QFile file("/etc/os-version");
    if (!file.open(QIODevice::ReadOnly)) {
        qCWarning(appLog) << "Failed to open /etc/os-version";
        return "";
    }
    QString info = file.readAll().data();
    QStringList lines = info.split("\n");
    foreach (const QString &line, lines) {
        if (line.startsWith("OsBuild")) {
            QStringList words = line.split("=");
            if (2 == words.size()) {
                return words[1].trimmed();
            }
        }
    }
    qCWarning(appLog) << "OsBuild not found in /etc/os-version";
    return "";
}

bool HttpDriverInterface::getVersion(QString &major, QString &minor)
{
    qCDebug(appLog) << "Get OS version";
    QFile file("/etc/os-version");
    if (!file.open(QIODevice::ReadOnly)) {
        qCWarning(appLog) << "Failed to open /etc/os-version";
        return false;
    }
    QString info = file.readAll().data();
    QStringList lines = info.split("\n");
    foreach (const QString &line, lines) {
        if (line.startsWith("MajorVersion")) {
            QStringList words = line.split("=");
            if (2 == words.size()) {
                major = words[1].trimmed();
            }
        }
        if (line.startsWith("MinorVersion")) {
            QStringList words = line.split("=");
            if (2 == words.size()) {
                minor = words[1].trimmed();
            }
        }
    }
    qCDebug(appLog) << "Found Major:" << major << "Minor:" << minor;
    return !major.isEmpty() && !minor.isEmpty();
}

bool HttpDriverInterface::convertJsonToDeviceList(QString strJson, QList<RepoDriverInfo> &lstDriverInfo)
{
    QJsonArray ja;
    QJsonArray jappds;
    QJsonArray jamodel;
    QJsonParseError json_error;
    QJsonDocument jsonDoc(QJsonDocument::fromJson(strJson.toLocal8Bit(), &json_error));

    lstDriverInfo.clear();
    qCDebug(appLog) << "Attempting to parse JSON driver list.";
    if (strJson.isEmpty() || json_error.error != QJsonParseError::NoError) {
        qCWarning(appLog) << "Invalid JSON data or parse error:" << json_error.errorString();
        return false;
    }
    if ("success" != jsonDoc.object().value("msg").toString()) {
        qCWarning(appLog) << "API returned error message:" << jsonDoc.object().value("msg").toString();
        return false;
    }
    ja = jsonDoc.object().value("data").toObject().value("list").toArray();
    if (ja.size() < 1) {
        qCDebug(appLog) << "JSON data list is empty.";
        return false;
    }

    qCDebug(appLog) << "Found" << ja.size() << "drivers in JSON data.";
    QJsonObject _jsonObj;
    QJsonObject _jsonObjppds;
    for (int i = 0; i < ja.size(); i++) {
        _jsonObj = ja.at(i).toObject();
        RepoDriverInfo _driverinfo;
        //因为是预先定义好的JSON数据格式，所以这里可以这样读取
        if (_jsonObj.contains("arch")) {
            _driverinfo.strArch = _jsonObj.value("arch").toString();
        }
        if (_jsonObj.contains("manufacturer")) {
            _driverinfo.strManufacturer = _jsonObj.value("manufacturer").toString();
        }
        if (_jsonObj.contains("deb_manufacturer")) {
            _driverinfo.strDebManufacturer = _jsonObj.value("deb_manufacturer").toString();
        }
        if (_jsonObj.contains("version")) {
            _driverinfo.strVersion = _jsonObj.value("version").toString();
        }
        if (_jsonObj.contains("deb_version")) {
            _driverinfo.strDebVersion = _jsonObj.value("deb_version").toString();
        }
        if (_jsonObj.contains("packages")) {
            _driverinfo.strPackages = _jsonObj.value("packages").toString();
        }
        if (_jsonObj.contains("class_p")) {
            _driverinfo.strClass_p = _jsonObj.value("class_p").toString();
        }
        if (_jsonObj.contains("class")) {
            _driverinfo.strClass = _jsonObj.value("class").toString();
        }
        if (_jsonObj.contains("models")) {
            //_driverinfo.strModels = ;
            jamodel = _jsonObj.value("models").toArray();
            for (int j = 0; j < jamodel.size(); j++) {
                _driverinfo.strModels.push_back(jamodel.at(j).toString());
            }
        }
        if (_jsonObj.contains("products")) {
            _driverinfo.strProducts = _jsonObj.value("products").toString();
        }
        if (_jsonObj.contains("deb")) {
            _driverinfo.strDeb = _jsonObj.value("deb").toString();
        }
        if (_jsonObj.contains("level")) {
            _driverinfo.iLevel = _jsonObj.value("level").toInt();
        }
        if (_jsonObj.contains("system")) {
            _driverinfo.strSystem = _jsonObj.value("system").toString();
        }
        if (_jsonObj.contains("desc")) {
            _driverinfo.strDesc = _jsonObj.value("desc").toString();
        }
        if (_jsonObj.contains("adaptation")) {
            _driverinfo.strAdaptation = _jsonObj.value("adaptation").toString();
        }
        if (_jsonObj.contains("source")) {
            _driverinfo.strSource = _jsonObj.value("source").toString();
        }
        if (_jsonObj.contains("download_url")) {
            _driverinfo.strDownloadUrl = _jsonObj.value("download_url").toString();
        }
        if (_jsonObj.contains("size")) {
            double size = _jsonObj.value("size").toInt();
            _driverinfo.bytes = qint64(size);
            if (size < 1024 * 1024) {
                _driverinfo.strSize = QString::number(size / 1024, 'f', 2) + "KB";
            } else if (size < 1024 * 1024 * 1024) {
                _driverinfo.strSize = QString::number(size / 1024 / 1024, 'f', 2) + "MB";
            } else {
                _driverinfo.strSize = QString::number(size / 1024 / 1024 / 1024, 'f', 2) + "GB";
            }
        }
        if (_jsonObj.contains("ppds")) {
            jappds = _jsonObj.value("ppds").toArray();
            strPpds _ppds;
            for (int j = 0; j < jappds.size(); j++) {
                _jsonObjppds = jappds.at(j).toObject();
                if (_jsonObjppds.contains("desc")) {
                    _ppds.strDesc = _jsonObjppds.value("desc").toString();
                }
                if (_jsonObjppds.contains("manufacturer")) {
                    _ppds.strManufacturer = _jsonObjppds.value("manufacturer").toString();
                }
                if (_jsonObjppds.contains("source")) {
                    _ppds.strSource = _jsonObjppds.value("source").toString();
                }
                _driverinfo.lstPpds.push_back(_ppds);
            }
        }

        lstDriverInfo.push_back(_driverinfo);
    }

    qCDebug(appLog) << "Successfully parsed" << lstDriverInfo.size() << "driver entries from JSON.";
    return true;
}

