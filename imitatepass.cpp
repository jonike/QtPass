#include "imitatepass.h"
#include "debughelper.h"
#include "mainwindow.h"
#include "qtpasssettings.h"

/**
 * @brief ImitatePass::ImitatePass for situaions when pass is not available
 * we imitate the behavior of pass https://www.passwordstore.org/
 */
ImitatePass::ImitatePass() {}

void ImitatePass::executeWrapper(int id, const QString &app,
                                 const QStringList &args, bool readStdout,
                                 bool readStderr) {
  executeWrapper(id, app, args, QString(), readStdout, readStderr);
}

/**
 * @brief ImitatePass::executeWrapper wrap the Executor for ease of use
 * @param id
 * @param app
 * @param args
 * @param input
 * @param readStdout
 * @param readStderr
 */
void ImitatePass::executeWrapper(int id, const QString &app,
                                 const QStringList &args, QString input,
                                 bool readStdout, bool readStderr) {
  QString d;
  for (auto &i : args)
    d += " " + i;
  dbg() << app << d;
  exec.execute(id, QtPassSettings::getPassStore(), app, args, input, readStdout,
               readStderr);
}

/**
 * @brief ImitatePass::GitInit git init wrapper
 */
void ImitatePass::GitInit() {
  executeWrapper(GIT_INIT, QtPassSettings::getGitExecutable(),
                 {"init", QtPassSettings::getPassStore()});
}

/**
 * @brief ImitatePass::GitPull git init wrapper
 */
void ImitatePass::GitPull() {
  executeWrapper(GIT_PULL, QtPassSettings::getGitExecutable(), {"pull"});
}

/**
 * @brief ImitatePass::GitPull_b git pull wrapper
 */
void ImitatePass::GitPull_b() {
  exec.executeBlocking(QtPassSettings::getGitExecutable(), {"pull"});
}

/**
 * @brief ImitatePass::GitPush git init wrapper
 */
void ImitatePass::GitPush() {
  executeWrapper(GIT_PUSH, QtPassSettings::getGitExecutable(), {"push"});
}

/**
 * @brief ImitatePass::Show shows content of file
 */
void ImitatePass::Show(QString file) {
  file = QtPassSettings::getPassStore() + file + ".gpg";
  QStringList args = {"-d",      "--quiet",     "--yes", "--no-encrypt-to",
                      "--batch", "--use-agent", file};
  executeWrapper(PASS_SHOW, QtPassSettings::getGpgExecutable(), args);
}

/**
 * @brief ImitatePass::Show_b show content of file, blocking version
 *
 * @returns process exitCode
 */
int ImitatePass::Show_b(QString file) {
  file = QtPassSettings::getPassStore() + file + ".gpg";
  QStringList args = {"-d",      "--quiet",     "--yes", "--no-encrypt-to",
                      "--batch", "--use-agent", file};
  return exec.executeBlocking(QtPassSettings::getGpgExecutable(), args);
}

/**
 * @brief ImitatePass::Insert create new file with encrypted content
 *
 * @param file      file to be created
 * @param value     value to be stored in file
 * @param overwrite whether to overwrite existing file
 */
void ImitatePass::Insert(QString file, QString newValue, bool overwrite) {
  file = QtPassSettings::getPassStore() + file + ".gpg";
  QStringList recipients = Pass::getRecipientList(file);
  if (recipients.isEmpty()) {
    //  TODO(bezet): probably throw here
    emit critical(tr("Can not edit"),
                  tr("Could not read encryption key to use, .gpg-id "
                     "file missing or invalid."));
    return;
  }
  QStringList args = {"--batch", "-eq", "--output", file};
  for (auto &r : recipients) {
    args.append("-r");
    args.append(r);
  };
  if (overwrite)
    args.append("--yes");
  args.append("-");
  executeWrapper(PASS_INSERT, QtPassSettings::getGpgExecutable(), args,
                 newValue);
  if (!QtPassSettings::isUseWebDav() && QtPassSettings::isUseGit()) {
    if (!overwrite)
      executeWrapper(GIT_ADD, QtPassSettings::getGitExecutable(),
                     {"add", file});
    QString path = QDir(QtPassSettings::getPassStore()).relativeFilePath(file);
    path.replace(QRegExp("\\.gpg$"), "");
    QString msg = QString(overwrite ? "Edit" : "\"Add") + " for " + path +
                  " using QtPass.";
    GitCommit(file, msg);
  }
}

/**
 * @brief ImitatePass::GitCommit commit a file to git with an appropriate commit
 * message
 * @param file
 * @param msg
 */
void ImitatePass::GitCommit(const QString &file, const QString &msg) {
  executeWrapper(GIT_COMMIT, QtPassSettings::getGitExecutable(),
                 {"commit", "-m", msg, "--", file});
}

/**
 * @brief ImitatePass::Remove git init wrapper
 */
void ImitatePass::Remove(QString file, bool isDir) {
  file = QtPassSettings::getPassStore() + file;
  if (!isDir)
    file += ".gpg";
  if (QtPassSettings::isUseGit()) {
    executeWrapper(GIT_RM, QtPassSettings::getGitExecutable(),
                   {"rm", (isDir ? "-rf" : "-f"), file});
    //  TODO(bezet): commit message used to have pass-like file name inside(ie.
    //  getFile(file, true)
    GitCommit(file, "Remove for " + file + " using QtPass.");
  } else {
    if (isDir) {
#if QT_VERSION >= QT_VERSION_CHECK(5, 0, 0)
      QDir dir(file);
      dir.removeRecursively();
#else
      removeDir(QtPassSettings::getPassStore() + file);
#endif
    } else
      QFile(file).remove();
  }
}

/**
 * @brief ImitatePass::Init initialize pass repository
 *
 * @param path      path in which new password-store will be created
 * @param users     list of users who shall be able to decrypt passwords in
 * path
 */
void ImitatePass::Init(QString path, const QList<UserInfo> &users) {
  QString gpgIdFile = path + ".gpg-id";
  QFile gpgId(gpgIdFile);
  bool addFile = false;
  if (QtPassSettings::isAddGPGId(true)) {
    QFileInfo checkFile(gpgIdFile);
    if (!checkFile.exists() || !checkFile.isFile())
      addFile = true;
  }
  if (!gpgId.open(QIODevice::WriteOnly | QIODevice::Text)) {
    emit critical(tr("Cannot update"),
                  tr("Failed to open .gpg-id for writing."));
    return;
  }
  bool secret_selected = false;
  foreach (const UserInfo &user, users) {
    if (user.enabled) {
      gpgId.write((user.key_id + "\n").toUtf8());
      secret_selected |= user.have_secret;
    }
  }
  gpgId.close();
  if (!secret_selected) {
    emit critical(
        tr("Check selected users!"),
        tr("None of the selected keys have a secret key available.\n"
           "You will not be able to decrypt any newly added passwords!"));
    return;
  }

  if (!QtPassSettings::isUseWebDav() && QtPassSettings::isUseGit() &&
      !QtPassSettings::getGitExecutable().isEmpty()) {
    if (addFile)
      executeWrapper(GIT_ADD, QtPassSettings::getGitExecutable(),
                     {"add", gpgIdFile});
    QString path = gpgIdFile;
    path.replace(QRegExp("\\.gpg$"), "");
    GitCommit(gpgIdFile, "Added " + path + " using QtPass.");
  }
  reencryptPath(path);
}

/**
 * @brief ImitatePass::removeDir delete folder recursive.
 * @param dirName which folder.
 * @return was removal succesful?
 */
bool ImitatePass::removeDir(const QString &dirName) {
  bool result = true;
  QDir dir(dirName);

  if (dir.exists(dirName)) {
    Q_FOREACH (QFileInfo info,
               dir.entryInfoList(QDir::NoDotAndDotDot | QDir::System |
                                     QDir::Hidden | QDir::AllDirs | QDir::Files,
                                 QDir::DirsFirst)) {
      if (info.isDir())
        result = removeDir(info.absoluteFilePath());
      else
        result = QFile::remove(info.absoluteFilePath());

      if (!result)
        return result;
    }
    result = dir.rmdir(dirName);
  }
  return result;
}

/**
 * @brief MainWindow::reencryptPath reencrypt all files under the chosen
 * directory
 *
 * This is stil quite experimental..
 * @param dir
 */
void ImitatePass::reencryptPath(QString dir) {
  emit statusMsg(tr("Re-encrypting from folder %1").arg(dir), 3000);
  emit startReencryptPath();
  if (QtPassSettings::isAutoPull()) {
    //  TODO(bezet): move statuses inside actions?
    emit statusMsg(tr("Updating password-store"), 2000);
    GitPull_b();
  }
  QDir currentDir;
  QDirIterator gpgFiles(dir, QStringList() << "*.gpg", QDir::Files,
                        QDirIterator::Subdirectories);
  QStringList gpgId;
  while (gpgFiles.hasNext()) {
    QString fileName = gpgFiles.next();
    if (gpgFiles.fileInfo().path() != currentDir.path()) {
      gpgId = getRecipientList(fileName);
      gpgId.sort();
    }
    //  TODO(bezet): enable --with-colons for better future-proofness?
    QStringList args = {
        "-v",          "--no-secmem-warning", "--no-permission-warning",
        "--list-only", "--keyid-format=long", fileName};
    QString keys, err;
    exec.executeBlocking(QtPassSettings::getGpgExecutable(), args, &keys, &err);
    QStringList actualKeys;
    keys += err;
    QStringList key = keys.split("\n");
    QListIterator<QString> itr(key);
    while (itr.hasNext()) {
      QString current = itr.next();
      QStringList cur = current.split(" ");
      if (cur.length() > 4) {
        QString actualKey = cur.takeAt(4);
        if (actualKey.length() == 16) {
          actualKeys << actualKey;
        }
      }
    }
    actualKeys.sort();
    if (actualKeys != gpgId) {
      // dbg()<< actualKeys << gpgId << getRecipientList(fileName);
      dbg() << "reencrypt " << fileName << " for " << gpgId;
      QString local_lastDecrypt = "Could not decrypt";
      emit lastDecrypt(local_lastDecrypt);
      args = QStringList{"-d",      "--quiet",     "--yes", "--no-encrypt-to",
                         "--batch", "--use-agent", fileName};
      exec.executeBlocking(QtPassSettings::getGpgExecutable(), args,
                           &local_lastDecrypt);
      emit lastDecrypt(local_lastDecrypt);

      if (!local_lastDecrypt.isEmpty() &&
          local_lastDecrypt != "Could not decrypt") {
        if (local_lastDecrypt.right(1) != "\n")
          local_lastDecrypt += "\n";

        emit lastDecrypt(local_lastDecrypt);
        QStringList recipients = Pass::getRecipientList(fileName);
        if (recipients.isEmpty()) {
          emit critical(tr("Can not edit"),
                        tr("Could not read encryption key to use, .gpg-id "
                           "file missing or invalid."));
          return;
        }
        args = QStringList{"--yes", "--batch", "-eq", "--output", fileName};
        for (auto &i : recipients) {
          args.append("-r");
          args.append(i);
        }
        args.append("-");
        exec.executeBlocking(QtPassSettings::getGpgExecutable(), args,
                             local_lastDecrypt);

        if (!QtPassSettings::isUseWebDav() && QtPassSettings::isUseGit()) {
          exec.executeBlocking(QtPassSettings::getGitExecutable(),
                               {"add", fileName});
          QString path =
              QDir(QtPassSettings::getPassStore()).relativeFilePath(fileName);
          path.replace(QRegExp("\\.gpg$"), "");
          exec.executeBlocking(QtPassSettings::getGitExecutable(),
                               {"commit", fileName, "-m",
                                "Edit for " + path + " using QtPass."});
        }

      } else {
        dbg() << "Decrypt error on re-encrypt";
      }
    }
  }
  if (QtPassSettings::isAutoPush()) {
    emit statusMsg(tr("Updating password-store"), 2000);
    GitPush();
  }
  emit endReencryptPath();
}