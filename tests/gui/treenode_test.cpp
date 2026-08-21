#include <gtest/gtest.h>

#include <QDebug>
#include <QString>

#include <gui/detail/b_treenode.h>

using namespace bakuon::gui;

struct Item
{
    std::string name;
    int weight   = 0;
    bool enabled = true;
};

static void printSubtree(TreeNode<Item>* n, std::size_t indent = 0)
{
    std::cout << std::string(indent * 2, ' ') << n->data().name << " (w=" << n->data().weight
              << ")\n";
    for (auto* c : n->children())
        printSubtree(c, indent + 1);
}

TEST(TreeTest, subtree)
{
    TreeNode<Item> root(Item{"root", 0});

    auto* a = root.appendChild(Item{"A", 10});
    auto* b = root.appendChild(Item{"B", 20});
    auto* c = root.appendChild(Item{"C", 30});

    auto* a1 = a->appendChild(Item{"A1", 1});
    a->appendChild(Item{"A2", 2});
    a->insertChildBefore(a1, Item{"A0", 0}); // O(1) 插入到 A1 之前

    auto* b1 = b->appendChild(Item{"B1", 21});
    b->appendChild(Item{"B2", 22, /*enabled=*/false});

    std::cout << "== 初始结构e ==\n";
    printSubtree(&root);

    std::cout << "\n== 拓扑查询 ==\n";
    std::cout << "childCount(root) = " << root.childCount() << '\n';   // 输出 3
    std::cout << "indexInParent(C) = " << c->index() << '\n';          // 输出 2
    std::cout << "depth(B1) = " << b1->depth() << '\n';                // 输出 2
    std::cout << "subtreeSize(root) = " << root.subtreeSize() << '\n'; // 输出 9

    std::cout << "\n== paths / pathNode 校验 ==\n";
    auto path = b1->paths();
    std::cout << "rowPath(B1) = [";
    for (auto p : path)
        std::cout << p << ' ';
    std::cout << "] -> resolves to " << root.pathNode(path)->data().name << '\n';
    std::vector<std::size_t> badPath{9, 9};
    std::cout << "isValidPath({9,9}) = " << std::boolalpha << root.isValidPath(badPath) << '\n';

    std::cout << "\n== 移动操作: 把 B1 移动为 C 的子节点 ==\n";
    b1->moveAsChild(c);
    printSubtree(&root);

    std::cout << "\n== 移动操作: 把 A 移动到 C 之前(同级兄弟移动) ==\n";
    a->moveBefore(c);
    printSubtree(&root);

    std::cout << "\n== 先序 / 后序 / 层序遍历(协程惰性视图) ==\n";
    std::cout << "PreOrder:  ";
    // 输出 root B B2 A A0 A1 A2 C B1
    for (auto* n : root.descendants(TraversalOrder::PreOrder))
        std::cout << n->data().name << ' ';
    std::cout << "\nPostOrder: ";
    // 输出 B2 B A0 A1 A2 A B1 C root
    for (auto* n : root.descendants(TraversalOrder::PostOrder))
        std::cout << n->data().name << ' ';
    std::cout << "\nLevelOrder:";
    // 输出 root B A C B2 A0 A1 A2 B1
    for (auto* n : root.descendants(TraversalOrder::LevelOrder))
        std::cout << ' ' << n->data().name;
    std::cout << '\n';

    std::cout << "\n== ranges 管道: 过滤 weight > 5 的节点，再取名字 ==\n";
    // 输出 B(20) B2(22) A(10) C(30) B1(21)
    for (auto* n :
         root.descendants() | std::views::filter([](auto* n) { return n->data().weight > 5; }))
        std::cout << n->data().name << '(' << n->data().weight << ") ";
    std::cout << '\n';

    std::cout << "\n== 多属性过滤视图: enabled == true 且 weight >= 2 ==\n";
    auto view = root.filteredAll([](const Item& it) { return it.enabled; },
                                 [](const Item& it) { return it.weight >= 2; });
    // 输出 B A A2 C B1
    for (auto* n : view)
        std::cout << n->data().name << ' ';
    std::cout << '\n';

    std::cout << "\n== 摘除并删除子树(移除 A，其下 A0/A1/A2 一并自动释放) ==\n";
    a->remove();
    printSubtree(&root);
    std::cout << "剩余节点总数 = " << root.subtreeSize() << '\n'; // 输出 5

    std::cout << "\n== 谱系查询: C1(原 B1) 的祖先链 ==\n";
    // 找到之前移动进 C 的节点(名字仍叫 B1)
    for (auto* n : c->children()) {
        if (n->data().name == "B1") {
            for (auto* anc : n->lineages())
                std::cout << anc->data().name << " <- ";
            std::cout << "(root)\n";
        }
    }
}
