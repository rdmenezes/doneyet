#include "project.h"
#include <map>
#include "dialog-box.h"
#include "hierarchical-list.h"
#include "utils.h"
#include "list-chooser.h"
#include "file-versions.h"

using std::map;
using std::cout;
using std::endl;

Project::Project(string name)
  : name_(name),
    list_(NULL) {
  ShowAllTasks();
  menubar_ = new MenuBar();
  Menu* m = menubar_->AddMenu("File");
  m->AddMenuItem("New");
  m->AddMenuItem("Open");
  m->AddMenuItem("Save");
  m->AddMenuItem("Quit");

  m = menubar_->AddMenu("View");
  m->AddMenuItem("Find...");
  m->AddMenuItem("All Tasks");
  m->AddMenuItem("Completed Tasks");
  m->AddMenuItem("Incomplete Tasks");
}

Project::~Project() {
  for (int i = 0; i < tasks_.size(); ++i) {
    tasks_[i]->Delete();
  }
  delete menubar_;
}

Project* Project::NewProject() {
  // Ask for a project name
  string answer = DialogBox::RunCentered(
      "Please Enter a Project Name",
      "Default Project");
  return new Project(answer);
}

void Project::FilterTasks(FilterPredicate<Task>* filter) {
  // Filter all the children of the root tasks.
  for (int i = 0; i < tasks_.size(); ++i) {
    tasks_[i]->ApplyFilter(filter);
  }

  // Finally filter the root tasks themselves.
  filtered_tasks_ = filter->FilterVector(tasks_);
}

void Project::DrawInWindow(WINDOW* win) {
  window_info info = CursesUtils::get_window_info(win);
  string name = "";
  ColumnSpec spec("Task:X,Created:24,Completed:24", false);
  list_ = new HierarchicalList(name,
      info.height,
      info.width,
      0,
      0,
      spec);
  list_->SetDatasource(this);
  list_->Draw();

  int ch;
  bool done = false;
  string tmp_str;
  while (!done && (ch = getch())) {
    Task* si = static_cast<Task*>(list_->SelectedItem());
    switch (ch) {
      case 'A':
        ShowAllTasks();
        list_->Update();
        list_->ScrollToTop();
        break;
      case 'a':
        tmp_str = DialogBox::RunMultiLine("Enter New Task",
            "",
            CursesUtils::winwidth(win) / 3,
            CursesUtils::winheight(win) / 3);
        if (!tmp_str.empty()) {
          Task* t = new Task(tmp_str, "");
          if (si == NULL) {
            tasks_.push_back(t);
          } else {
            si->AddSubTask(t);
          }
        }
        ComputeNodeStatus();
        FilterTasks();
        list_->Update();
        break;
      case 'M':
        {
          menubar_->ShowNextMenu();
          string answer;
          bool mdone = false;
          while (!mdone) {
            ch = getch();
            switch (ch) {
              case KEY_DOWN:
              case 'j':
                menubar_->SendEventToMenu(REQ_DOWN_ITEM);
                break;
              case KEY_UP:
              case 'k':
                menubar_->SendEventToMenu(REQ_UP_ITEM);
                break;
              case KEY_RIGHT:
              case 'l':
                list_->Draw();
                menubar_->ShowNextMenu();
                break;
              case KEY_LEFT:
              case 'h':
                list_->Draw();
                menubar_->ShowPreviousMenu();
                break;
              case '\r':
                answer = menubar_->SelectedItem();
                mdone = true;
                break;
              case 27:  // escape
                answer = "";
                mdone = true;
                break;
            }
          }
          if (!answer.empty())
            HandleMenuInput(answer);
          if (answer == "Quit") {
            beep();
            done = true;
          }
        }
        break;
      case 'm':
        if (si) {
          // Get another character to see if they want to move up or down:
          ch = getch();
          switch (ch) {
            case 'u':
              si->MoveUp();
              FilterTasks();
              list_->Update();
              break;
            case 'd':
              si->MoveDown();
              FilterTasks();
              list_->Update();
              break;
          }
        }
        break;
      case 'n':
        if (si) {
          tmp_str = DialogBox::RunCenteredWithWidth("Add Note",
              "",
              CursesUtils::winwidth(stdscr) / 3);
          if (!tmp_str.empty()) {
            si->AddNote(tmp_str);
          }
        }
        break;
      case 'v':
        if (si) 
          ListChooser::GetChoice(si->Notes());
        break;
      case 'j':
      case KEY_DOWN:
        list_->SelectNextItem();
        break;
      case KEY_UP:
      case 'k':
        list_->SelectPrevItem();
        break;
      case 'r':
        Rename();
        break;
      case 'e':
        list_->EditSelectedItem();
        break;
      case 'd':
        list_->SelectPrevItem();
        DeleteTask(si);
        ComputeNodeStatus();
        FilterTasks();
        list_->Update();
        break;
      case 'c':
        list_->ToggleExpansionOfSelectedItem();
        break;
      case 'R':
        ArchiveCompletedTasks();
        list_->Update();
        list_->ScrollToTop();
        break;
      case 'C':
        ShowCompletedLastWeek();
        list_->Update();
        list_->ScrollToTop();
        break;
      case 'f':
        {
          string tmp_str = DialogBox::RunCenteredWithWidth("Enter Search Term:",
              "",
              CursesUtils::winwidth(win) / 3);
          if (!tmp_str.empty()) {
            RunSearchFilter(tmp_str);
            list_->Update();
            list_->ScrollToTop();
          }
          break;
        }
      case 27: // escape
        list_->SelectNoItem();
        break;
      case 32: // space
        if (si && !si->NumChildren()) {
          switch (si->Status()) {
            case CREATED:
            case PAUSED:
              si->SetStatus(IN_PROGRESS);
              break;
            case IN_PROGRESS:
              si->SetStatus(COMPLETED);
              break;
            case COMPLETED:
              si->SetStatus(PAUSED);
              break;
          }
          ComputeNodeStatus();
        }
        break;
      case '\t':
        list_->SelectNextColumn();
        break;
      case '\r':
      case 'q':
        done = true;
    }
    list_->Draw();
  }
}

Task* Project::AddTaskNamed(const string& name) {
  Task* nt = new Task(name, "");
  tasks_.push_back(nt);  
  return nt;
}

void Project::Serialize(Serializer* s) {
  // Write a serialization version.
  s->WriteInt64(NOTES_VERSION);

  // Write our project name to the file.
  s->WriteString(name_);

  // Write how many tasks there are.
  s->WriteInt32(NumTasks());

  // Serialize the tree.
  for (int i = 0; i < tasks_.size(); ++i) {
    tasks_[i]->Serialize(s);
  }
}

Project* Project::NewProjectFromFile(string path) {
  // Create the serializer
  printf("Going to open \"%s\"\n", path.c_str());
  Serializer s(path, "");
  if (!s.Okay()) {
    cout << "Error opening saved project: \"" << path << "\"" << endl;
    return NULL;
  }
  
  // Read the file version
  uint64 file_version = s.ReadUint64();
  s.SetVersion(file_version);
  
  // Find the project's name
  string project_name = s.ReadString();
  printf("PROJECT NAME: %s\n", project_name.c_str());
  Project* p = new Project(project_name);

  // Find how many tasks there are.
  int num_tasks = s.ReadInt32();
  cout << "There are " << num_tasks << " tasks." << endl;
  
  // First read in every task in the file.
  int task_identifier;
  int parent_pointer;
  map<int, Task*> task_map;
  map<Task*, int> tasks_parents;
  vector<Task*> tasks;
  for (int i = 0; i < num_tasks; ++i) {
    // Read in the values.
    task_identifier = s.ReadUint64();
    Task* t = Task::NewTaskFromSerializer(&s);
    parent_pointer = s.ReadUint64();

    tasks_parents[t] = parent_pointer;
    task_map[task_identifier] = t;
    tasks.push_back(t);
  }
  
  // Then re-assemble the tree structure.
  for (int i = 0; i < tasks.size(); ++i) {
    Task* t = tasks[i];
    if (tasks_parents[t] == 0) {
      // We have a root task.  Add it to the root list_.
      p->tasks_.push_back(tasks[i]);
    } else {
      // We have a child task.  Add it to its parent's list_.
      task_map[tasks_parents[t]]->AddSubTask(tasks[i]);
    }
  }

  p->ShowAllTasks();
  return p;
}

int Project::NumTasks() {
  int total = 0;
  for (int i = 0; i < tasks_.size(); ++i) {
    total += 1 + tasks_[i]->NumOffspring();
  }
  return total;
}

void Project::DeleteTask(Task* t) {
  if (t->Parent() == NULL) {
    // It's a top level task.  Remove it from our list of roots.
    for (int i = 0; i < tasks_.size(); ++i) {
      if (tasks_[i] == t) {
        tasks_[i]->Delete();
        tasks_.erase(tasks_.begin() + i);
        break;
      }
    }
  } else {
    for (int i = 0; i < tasks_.size(); ++i) {
      tasks_[i]->DeleteTask(t);
    }
  }
}

void Project::Rename() {
  string answer = DialogBox::RunCenteredWithWidth(
      "Rename Project",
      name_,
      20);
  if (!answer.empty())
    name_ = answer;
}

// Compute the status of all nodes.  Nodes which have children have their status
// for them (hence the need for this function).  A node with any IN_PROGRESS
// child is itself IN_PROGRESS.  If all of a node's children are PAUSED, the
// node is PAUSED.
void Project::ComputeNodeStatus() {
  for (int i = 0; i < tasks_.size(); ++i) {
    ComputeStatusForTask(tasks_[i]);
  }
}

TaskStatus Project::ComputeStatusForTask(Task* t) {
  if (!t->NumChildren()) {
    return t->Status();
  }

  int status_counts[NUM_STATUSES];
  for (int i = 0; i < NUM_STATUSES; ++i) {
    status_counts[i] = 0;
  }

  for (int i = 0; i < t->NumChildren(); ++i) {
    TaskStatus s = ComputeStatusForTask(t->Child(i));
    ++status_counts[s];
  }

  // If any children are in progress, so is this one.
  if (status_counts[IN_PROGRESS]) {
    t->SetStatus(IN_PROGRESS);
    return IN_PROGRESS;
  }

  // If all children are the same, this node gets that status.
  for (int i = 0; i < NUM_STATUSES; ++i) {
    if (status_counts[i] == t->NumChildren()) {
      t->SetStatus((TaskStatus) i);
      return (TaskStatus) i;
    }
  }

  // Under any other circumstance we're in progress.
  t->SetStatus(IN_PROGRESS);
  return IN_PROGRESS;
}

int Project::NumFilteredRoots() {
  return filtered_tasks_.size();
}

Task* Project::FilteredRoot(int r) {
  return filtered_tasks_[r];
}

void Project::ShowAllTasks() {
  GTFilterPredicate<Task, time_t>* gtfp =
    new GTFilterPredicate<Task, time_t>(-1, Task::CompletionDateWrapper);
  base_filter_.Clear();
  base_filter_.AddChild(gtfp);
  FilterTasks();
}

void Project::ArchiveCompletedTasks() {
  EqualityFilterPredicate<Task, TaskStatus>* efp =
    new EqualityFilterPredicate<Task, TaskStatus>(COMPLETED, Task::StatusWrapper);
  efp->SetIsNot(true);
  base_filter_.Clear();
  base_filter_.AddChild(efp);
  FilterTasks();
}

void Project::ShowCompletedLastWeek() {
  // We check if their completion date is > one week ago.
  time_t week_ago;
  std::time(&week_ago);
  week_ago -= 60 * 60 * 24 * 7;
  GTFilterPredicate<Task, time_t>* gtfp =
    new GTFilterPredicate<Task, time_t>(week_ago, Task::CompletionDateWrapper);
  GTFilterPredicate<Task, int>* has_filtered_child =
    new GTFilterPredicate<Task, int>(0, Task::NumFilteredOffspringWrapper);
  OrFilterPredicate<Task>* or_filter = new OrFilterPredicate<Task>();
  or_filter->AddChild(gtfp);
  or_filter->AddChild(has_filtered_child);
  base_filter_.Clear();
  base_filter_.AddChild(or_filter);
  FilterTasks();
}

void Project::RunSearchFilter(const string& needle) {
  // Clear the current filters.
  base_filter_.Clear();

  // Create the search filters.  Right now it only searches on the text and
  // description of the tasks.
  StringContainsFilterPredicate<Task>* title_filter =
    new StringContainsFilterPredicate<Task>(needle, Task::TitleWrapper);

  StringContainsFilterPredicate<Task>* description_filter =
    new StringContainsFilterPredicate<Task>(needle, Task::DescriptionWrapper);

  GTFilterPredicate<Task, int>* has_filtered_child =
    new GTFilterPredicate<Task, int>(0, Task::NumFilteredOffspringWrapper);

  OrFilterPredicate<Task>* or_filter = new OrFilterPredicate<Task>();
  or_filter->AddChild(title_filter);
  or_filter->AddChild(description_filter);
  or_filter->AddChild(has_filtered_child);

  base_filter_.AddChild(or_filter);
  FilterTasks();
}

void Project::HandleMenuInput(const string& input) {
  if (input == "All Tasks") {
    ShowAllTasks();
    list_->Update();
  } else if (input == "Completed Tasks") {
    ShowCompletedLastWeek();
    list_->Update();
  } else if (input == "Incomplete Tasks") {
    ArchiveCompletedTasks();
    list_->Update();
  } else if (input == "Find...") {
    string tmp_str = DialogBox::RunCenteredWithWidth("Enter Search Term:",
        "",
        CursesUtils::winwidth(stdscr) / 3);
    if (!tmp_str.empty()) {
      RunSearchFilter(tmp_str);
      list_->Update();
      list_->ScrollToTop();
    }
  }
}
