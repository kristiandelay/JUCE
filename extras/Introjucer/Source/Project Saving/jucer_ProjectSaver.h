/*
  ==============================================================================

   This file is part of the JUCE library - "Jules' Utility Class Extensions"
   Copyright 2004-11 by Raw Material Software Ltd.

  ------------------------------------------------------------------------------

   JUCE can be redistributed and/or modified under the terms of the GNU General
   Public License (Version 2), as published by the Free Software Foundation.
   A copy of the license is included in the JUCE distribution, or can be found
   online at www.gnu.org/licenses.

   JUCE is distributed in the hope that it will be useful, but WITHOUT ANY
   WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS FOR
   A PARTICULAR PURPOSE.  See the GNU General Public License for more details.

  ------------------------------------------------------------------------------

   To release a closed-source product which uses JUCE, commercial licenses are
   available: visit www.rawmaterialsoftware.com/juce for more information.

  ==============================================================================
*/

#ifndef __JUCER_PROJECTSAVER_JUCEHEADER__
#define __JUCER_PROJECTSAVER_JUCEHEADER__

#include "jucer_ResourceFile.h"
#include "../Project/jucer_Module.h"
#include "jucer_ProjectExporter.h"


//==============================================================================
class ProjectSaver
{
public:
    ProjectSaver (Project& project_, const File& projectFile_)
        : project (project_),
          projectFile (projectFile_),
          generatedCodeFolder (project.getGeneratedCodeFolder()),
          generatedFilesGroup (Project::Item::createGroup (project, getJuceCodeGroupName(), "__generatedcode__"))
    {
        generatedFilesGroup.setID (getGeneratedGroupID());

        if (generatedCodeFolder.exists())
            deleteNonHiddenFilesIn (generatedCodeFolder);
    }

    Project& getProject() noexcept      { return project; }

    String save()
    {
        jassert (generatedFilesGroup.getNumChildren() == 0); // this method can't be called more than once!

        const File oldFile (project.getFile());
        project.setFile (projectFile);

        writeMainProjectFile();

        OwnedArray<LibraryModule> modules;

        {
            ModuleList moduleList;
            moduleList.rescan (ModuleList::getDefaultModulesFolder (&project));
            project.createRequiredModules (moduleList, modules);
        }

        if (errors.size() == 0)
            writeAppConfigFile (modules);

        if (errors.size() == 0)
            writeBinaryDataFiles();

        if (errors.size() == 0)
            writeAppHeader (modules);

        if (errors.size() == 0)
            writeProjects (modules);

        if (errors.size() == 0)
            writeAppConfigFile (modules); // (this is repeated in case the projects added anything to it)

        if (generatedCodeFolder.exists() && errors.size() == 0)
            writeReadmeFile();

        if (errors.size() > 0)
            project.setFile (oldFile);

        return errors[0];
    }

    Project::Item saveGeneratedFile (const String& filePath, const MemoryOutputStream& newData)
    {
        if (! generatedCodeFolder.createDirectory())
        {
            errors.add ("Couldn't create folder: " + generatedCodeFolder.getFullPathName());
            return Project::Item (project, ValueTree::invalid);
        }

        const File file (generatedCodeFolder.getChildFile (filePath));

        if (replaceFileIfDifferent (file, newData))
            return addFileToGeneratedGroup (file);

        return Project::Item (project, ValueTree::invalid);
    }

    Project::Item addFileToGeneratedGroup (const File& file)
    {
        Project::Item item (generatedFilesGroup.findItemForFile (file));

        if (! item.isValid())
        {
            generatedFilesGroup.addFile (file, -1, true);
            item = generatedFilesGroup.findItemForFile (file);
        }

        return item;
    }

    void setExtraAppConfigFileContent (const String& content)
    {
        extraAppConfigContent = content;
    }

    static void writeAutoGenWarningComment (OutputStream& out)
    {
        out << "/*" << newLine << newLine
            << "    IMPORTANT! This file is auto-generated each time you save your" << newLine
            << "    project - if you alter its contents, your changes may be overwritten!" << newLine
            << newLine;
    }

    static const char* getGeneratedGroupID() noexcept       { return "__jucelibfiles"; }
    Project::Item& getGeneratedCodeGroup()                  { return generatedFilesGroup; }

    static String getJuceCodeGroupName()                    { return "Juce Library Code"; }

    File getGeneratedCodeFolder() const                     { return generatedCodeFolder; }

    bool replaceFileIfDifferent (const File& f, const MemoryOutputStream& newData)
    {
        if (! FileHelpers::overwriteFileWithNewDataIfDifferent (f, newData))
        {
            errors.add ("Can't write to file: " + f.getFullPathName());
            return false;
        }

        return true;
    }

private:
    Project& project;
    const File projectFile, generatedCodeFolder;
    Project::Item generatedFilesGroup;
    String extraAppConfigContent;
    StringArray errors;

    File appConfigFile, binaryDataCpp;

    // Recursively clears out a folder's contents, but leaves behind any folders
    // containing hidden files used by version-control systems.
    static bool deleteNonHiddenFilesIn (const File& parent)
    {
        bool folderIsNowEmpty = true;
        DirectoryIterator i (parent, false, "*", File::findFilesAndDirectories);
        Array<File> filesToDelete;

        bool isFolder;
        while (i.next (&isFolder, nullptr, nullptr, nullptr, nullptr, nullptr))
        {
            const File f (i.getFile());

            if (shouldFileBeKept (f.getFileName()))
            {
                folderIsNowEmpty = false;
            }
            else if (isFolder)
            {
                if (deleteNonHiddenFilesIn (f))
                    filesToDelete.add (f);
                else
                    folderIsNowEmpty = false;
            }
            else
            {
                filesToDelete.add (f);
            }
        }

        for (int j = filesToDelete.size(); --j >= 0;)
            filesToDelete.getReference(j).deleteRecursively();

        return folderIsNowEmpty;
    }

    static bool shouldFileBeKept (const String& filename)
    {
        const char* filesToKeep[] = { ".svn", ".cvs", "CMakeLists.txt" };

        for (int i = 0; i < numElementsInArray (filesToKeep); ++i)
            if (filename == filesToKeep[i])
                return true;

        return false;
    }

    void writeMainProjectFile()
    {
        ScopedPointer <XmlElement> xml (project.getProjectRoot().createXml());
        jassert (xml != nullptr);

        if (xml != nullptr)
        {
           #if JUCE_DEBUG
            {
                MemoryOutputStream mo;
                project.getProjectRoot().writeToStream (mo);

                MemoryInputStream mi (mo.getData(), mo.getDataSize(), false);
                ValueTree v = ValueTree::readFromStream (mi);
                ScopedPointer <XmlElement> xml2 (v.createXml());

                // This bit just tests that ValueTree save/load works reliably.. Let me know if this asserts for you!
                jassert (xml->isEquivalentTo (xml2, true));
            }
           #endif

            MemoryOutputStream mo;
            xml->writeToStream (mo, String::empty);
            replaceFileIfDifferent (projectFile, mo);
        }
    }

    static int findLongestModuleName (const OwnedArray<LibraryModule>& modules)
    {
        int longest = 0;

        for (int i = modules.size(); --i >= 0;)
            longest = jmax (longest, modules.getUnchecked(i)->getID().length());

        return longest;
    }

    void writeAppConfig (OutputStream& out, const OwnedArray<LibraryModule>& modules)
    {
        writeAutoGenWarningComment (out);
        out << "    If you want to change any of these values, use the Introjucer to do so," << newLine
            << "    rather than editing this file directly!" << newLine
            << newLine
            << "    Any commented-out settings will assume their default values." << newLine
            << newLine
            << "*/" << newLine
            << newLine;

        const String headerGuard ("__JUCE_APPCONFIG_" + project.getProjectUID().toUpperCase() + "__");
        out << "#ifndef " << headerGuard << newLine
            << "#define " << headerGuard << newLine
            << newLine
            << "//==============================================================================" << newLine;

        const int longestName = findLongestModuleName (modules);

        for (int k = 0; k < modules.size(); ++k)
        {
            LibraryModule* const m = modules.getUnchecked(k);
            out << "#define JUCE_MODULE_AVAILABLE_" << m->getID()
                << String::repeatedString (" ", longestName + 5 - m->getID().length()) << " 1" << newLine;
        }

        out << newLine;

        for (int j = 0; j < modules.size(); ++j)
        {
            LibraryModule* const m = modules.getUnchecked(j);
            OwnedArray <Project::ConfigFlag> flags;
            m->getConfigFlags (project, flags);

            if (flags.size() > 0)
            {
                out << "//==============================================================================" << newLine
                    << "// " << m->getID() << " flags:" << newLine
                    << newLine;

                for (int i = 0; i < flags.size(); ++i)
                {
                    flags.getUnchecked(i)->value.referTo (project.getConfigFlag (flags.getUnchecked(i)->symbol));

                    const Project::ConfigFlag* const f = flags[i];
                    const String value (project.getConfigFlag (f->symbol).toString());

                    if (value == Project::configFlagEnabled)
                        out << "#define    " << f->symbol << " 1";
                    else if (value == Project::configFlagDisabled)
                        out << "#define    " << f->symbol << " 0";
                    else
                        out << "//#define  " << f->symbol;

                    out << newLine;
                }

                if (j < modules.size() - 1)
                    out << newLine;
            }
        }

        if (extraAppConfigContent.isNotEmpty())
            out << newLine << extraAppConfigContent.trimEnd() << newLine;

        out << newLine
            << "#endif  // " << headerGuard << newLine;
    }

    void writeAppConfigFile (const OwnedArray<LibraryModule>& modules)
    {
        appConfigFile = generatedCodeFolder.getChildFile (project.getAppConfigFilename());

        MemoryOutputStream mem;
        writeAppConfig (mem, modules);
        saveGeneratedFile (project.getAppConfigFilename(), mem);
    }

    void writeAppHeader (OutputStream& out, const OwnedArray<LibraryModule>& modules)
    {
        writeAutoGenWarningComment (out);

        out << "    This is the header file that your files should include in order to get all the" << newLine
            << "    JUCE library headers. You should avoid including the JUCE headers directly in" << newLine
            << "    your own source files, because that wouldn't pick up the correct configuration" << newLine
            << "    options for your app." << newLine
            << newLine
            << "*/" << newLine << newLine;

        String headerGuard ("__APPHEADERFILE_" + project.getProjectUID().toUpperCase() + "__");
        out << "#ifndef " << headerGuard << newLine
            << "#define " << headerGuard << newLine << newLine;

        if (appConfigFile.exists())
            out << CodeHelpers::createIncludeStatement (project.getAppConfigFilename()) << newLine;

        for (int i = 0; i < modules.size(); ++i)
            modules.getUnchecked(i)->writeIncludes (*this, out);

        if (binaryDataCpp.exists())
            out << CodeHelpers::createIncludeStatement (binaryDataCpp.withFileExtension (".h"), appConfigFile) << newLine;

        out << newLine
            << "#if ! DONT_SET_USING_JUCE_NAMESPACE" << newLine
            << " // If your code uses a lot of JUCE classes, then this will obviously save you" << newLine
            << " // a lot of typing, but can be disabled by setting DONT_SET_USING_JUCE_NAMESPACE." << newLine
            << " using namespace juce;" << newLine
            << "#endif" << newLine
            << newLine
            << "namespace ProjectInfo" << newLine
            << "{" << newLine
            << "    const char* const  projectName    = " << CodeHelpers::addEscapeChars (project.getProjectName().toString()).quoted() << ";" << newLine
            << "    const char* const  versionString  = " << CodeHelpers::addEscapeChars (project.getVersion().toString()).quoted() << ";" << newLine
            << "    const int          versionNumber  = " << project.getVersionAsHex() << ";" << newLine
            << "}" << newLine
            << newLine
            << "#endif   // " << headerGuard << newLine;
    }

    void writeAppHeader (const OwnedArray<LibraryModule>& modules)
    {
        MemoryOutputStream mem;
        writeAppHeader (mem, modules);
        saveGeneratedFile (project.getJuceSourceHFilename(), mem);
    }

    void writeBinaryDataFiles()
    {
        binaryDataCpp = generatedCodeFolder.getChildFile ("BinaryData.cpp");

        ResourceFile resourceFile (project);

        if (resourceFile.getNumFiles() > 0)
        {
            resourceFile.setClassName ("BinaryData");

            if (resourceFile.write (binaryDataCpp))
            {
                generatedFilesGroup.addFile (binaryDataCpp, -1, true);
                generatedFilesGroup.addFile (binaryDataCpp.withFileExtension (".h"), -1, false);
            }
            else
            {
                errors.add ("Can't create binary resources file: " + binaryDataCpp.getFullPathName());
            }
        }
        else
        {
            binaryDataCpp.deleteFile();
            binaryDataCpp.withFileExtension ("h").deleteFile();
        }
    }

    void writeReadmeFile()
    {
        MemoryOutputStream out;
        out << newLine
            << " Important Note!!" << newLine
            << " ================" << newLine
            << newLine
            << "The purpose of this folder is to contain files that are auto-generated by the Introjucer," << newLine
            << "and ALL files in this folder will be mercilessly DELETED and completely re-written whenever" << newLine
            << "the Introjucer saves your project." << newLine
            << newLine
            << "Therefore, it's a bad idea to make any manual changes to the files in here, or to" << newLine
            << "put any of your own files in here if you don't want to lose them. (Of course you may choose" << newLine
            << "to add the folder's contents to your version-control system so that you can re-merge your own" << newLine
            << "modifications after the Introjucer has saved its changes)." << newLine;

        replaceFileIfDifferent (generatedCodeFolder.getChildFile ("ReadMe.txt"), out);
    }

    static void sortGroupRecursively (Project::Item group)
    {
        group.sortAlphabetically (true);

        for (int i = group.getNumChildren(); --i >= 0;)
            sortGroupRecursively (group.getChild(i));
    }

    void writeProjects (const OwnedArray<LibraryModule>& modules)
    {
        // keep a copy of the basic generated files group, as each exporter may modify it.
        const ValueTree originalGeneratedGroup (generatedFilesGroup.getNode().createCopy());

        for (int i = project.getNumExporters(); --i >= 0;)
        {
            ScopedPointer <ProjectExporter> exporter (project.createExporter (i));
            std::cout << "Writing files for: " << exporter->getName() << std::endl;

            if (exporter->getTargetFolder().createDirectory())
            {
                exporter->addToExtraSearchPaths (RelativePath ("JuceLibraryCode", RelativePath::projectFolder));

                generatedFilesGroup.getNode() = originalGeneratedGroup.createCopy();
                project.getProjectType().prepareExporter (*exporter);

                for (int j = 0; j < modules.size(); ++j)
                    modules.getUnchecked(j)->prepareExporter (*exporter, *this);

                sortGroupRecursively (generatedFilesGroup);
                exporter->groups.add (generatedFilesGroup);

                try
                {
                    exporter->create();
                }
                catch (ProjectExporter::SaveError& error)
                {
                    errors.add (error.message);
                }
            }
            else
            {
                errors.add ("Can't create folder: " + exporter->getTargetFolder().getFullPathName());
            }
        }
    }

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (ProjectSaver);
};


#endif   // __JUCER_PROJECTSAVER_JUCEHEADER__