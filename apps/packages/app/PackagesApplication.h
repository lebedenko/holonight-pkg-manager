#pragma once

#include <QGuiApplication>

#include <memory>

class QQuickView;
class InstalledPackagesModel;

class PackagesApplication : public QGuiApplication {
  Q_OBJECT

 public:
  PackagesApplication(int& argc, char** argv);
  ~PackagesApplication() override;

  PackagesApplication(const PackagesApplication&) = delete;
  PackagesApplication& operator=(const PackagesApplication&) = delete;
  PackagesApplication(PackagesApplication&&) = delete;
  PackagesApplication& operator=(PackagesApplication&&) = delete;

 private:
  std::unique_ptr<InstalledPackagesModel> installed_packages_model_;
  std::unique_ptr<QQuickView> view_;
};
