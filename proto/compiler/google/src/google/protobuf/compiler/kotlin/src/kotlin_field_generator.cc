//
// Created by user on 7/12/16.
//

#include "kotlin_field_generator.h"
#include <vector>
#include <google/protobuf/io/printer.h>
#include <google/protobuf/descriptor.h>
#include "kotlin_name_resolver.h"
#include "UnreachableStateException.h"
#include <iostream>

namespace google {
namespace protobuf {
namespace compiler {
namespace kotlin {

string FieldGenerator::getInitValue() const {
    if (getProtoLabel() == FieldDescriptor::LABEL_REPEATED) {
        return getFullType() + "(0)";
    }
    if (getProtoType() == FieldDescriptor::TYPE_MESSAGE) {
        return getBuilderFullType() + "().build()";
    }
    if (getProtoType() == FieldDescriptor::TYPE_ENUM) {
        return getEnumFromIntConverter() + "(0)";
    }
    return name_resolving::protobufTypeToInitValue(getProtoType());
}

void FieldGenerator::generateCode(io::Printer *printer, bool isBuilder) const {
    map<string, string> vars;
    vars["name"] = simpleName;
    vars["field"] = getFullType();
    printer->Print(vars, "var $name$ : $field$\n");

    // make setter private
    printer->Indent();
    printer->Print("private set\n");
    printer->Outdent();

    // generate setter for builder
    if (isBuilder) {
        generateSetter(printer);
    }

    // generate additional methods for repeated fields
    if (getProtoLabel() == FieldDescriptor::LABEL_REPEATED) {
        generateRepeatedMethods(printer, isBuilder);
    }
}

FieldGenerator::FieldGenerator(FieldDescriptor const * descriptor, ClassGenerator const * enclosingClass, NameResolver * nameResolver)
        : descriptor(descriptor)
        , enclosingClass(enclosingClass)
        , nameResolver(nameResolver)
        , simpleName(descriptor->name())
        , protoLabel(descriptor->label())
{ }

void FieldGenerator::generateSerializationForPacked(io::Printer *printer, bool isRead, bool noTag, bool isField) const {
    map <string, string> vars;
    vars["fieldNumber"] = std::to_string(getFieldNumber());
    vars["underlyingType"] = getUnderlyingTypeGenerator().getFullType();
    vars["initValue"] = getUnderlyingTypeGenerator().getInitValue();
    vars["fieldName"] = simpleName;
    vars["arrayType"] = getFullType();

    bool isPrimitive = descriptor->type() != FieldDescriptor::TYPE_BYTES &&
                       descriptor->type() != FieldDescriptor::TYPE_MESSAGE &&
                       descriptor->type() != FieldDescriptor::TYPE_STRING &&
                       descriptor->type() != FieldDescriptor::TYPE_ENUM;

    if (isRead) {
        if (!noTag) {
            printer->Print(vars, "val tag = input.readTag($fieldNumber$, WireType.LENGTH_DELIMITED)\n");
        }
        printer->Print(vars, "val expectedByteSize = input.readInt32NoTag()\n");

        /* Now we want to compute element size of array. For this, we are estimating byte size of a single element,
         * and then divide byte size of whole array by byte size of single element.
         */
        printer->Print(vars, "var singleElemSize = 0\n");

        /* hack: copy current FieldGenerator and change label to OPTIONAL. Also change name to
           name of iterator in for-loop.
           This will allow to re-use this function for generating serialization code for elements of array.
           More importantly, this will care about nested types too.
           Efficiently, it inlines serialization code for all underlying types.
           This hack isn't necessary from the architectural point of view and could be safely
           removed as soon as target code will support inheritance and interfaces.
           (then writing CodedOutputStream.writeMessage will be possible).
         */
        FieldGenerator singleFieldGen = getUnderlyingTypeGenerator();
        singleFieldGen.simpleName = "singleElemSize";
        singleFieldGen.generateSerializationCode(printer, isRead, /* noTag = */ true, /* isField = */ false);

        printer->Print(vars, "val arraySize = expectedByteSize / singleElemSize\n");

        // Allocate new array of estimated size
        printer->Print(vars, "val newArray = $arrayType$(arraySize)\n");

        printer->Print("var i = 0\n");
        printer->Print(vars, "while(i < arraySize) {\n");
        printer->Indent();

        printer->Print(vars, "var tmp: $underlyingType$ = $initValue$\n");
        singleFieldGen.simpleName = "tmp";
        singleFieldGen.protoLabel = FieldDescriptor::LABEL_OPTIONAL;

        singleFieldGen.generateSerializationCode(printer, isRead, /* noTag = */ true, /* isField = */ false);

        printer->Print(vars, "$fieldName$[i] = tmp\n");
        printer->Print("i += 1\n");

        printer->Outdent();
        printer->Print("}\n");
    }
    else {
        /**
         * Protobuf format:
         * - Check if size of array is > 0, because empty repeated fields shouldn't appear in message
         * - Write tag explicitly
         * - Write length as int32 (note that tag shouldn't be added)
         * - Write all repeated elements via recursive call (for primitive types without tags)
         */
        // tag
        printer->Print(vars, "output.writeTag($fieldNumber$, WireType.LENGTH_DELIMITED)\n");

        // length
        printer->Print(vars, "var arrayByteSize = 0\n");
        generateSizeEstimationCode(printer, "arrayByteSize", /* noTag = */ true, /* isField = */ false);
        printer->Print(vars, "output.writeInt32NoTag(arrayByteSize)\n");

        // all elements
        printer->Print(vars, "for (item in $fieldName$) {\n");
        printer->Indent();

        // hack: see above
        FieldGenerator singleFieldGen = FieldGenerator(descriptor, enclosingClass, nameResolver);
        singleFieldGen.simpleName = "item";
        singleFieldGen.protoLabel = FieldDescriptor::LABEL_OPTIONAL;

        singleFieldGen.generateSerializationCode(printer, isRead, /* noTag = */ isPrimitive, /* isField = */ false);

        printer->Outdent(); // for-loop
        printer->Print("}\n");
    }
}

void FieldGenerator::generateSerializationForEnums(io::Printer * printer, bool isRead, bool noTag, bool isField) const {

    map <string, string> vars;
    vars["converter"] = getEnumFromIntConverter();
    vars["fieldName"] = simpleName;
    vars["suffix"] = getKotlinFunctionSuffix();
    vars["fieldNumber"] = std::to_string(getFieldNumber());
    vars["initValue"] = getInitValue();

    if (isRead) {
        if (noTag) {
            printer->Print(vars, "$fieldName$ = $converter$(input.read$suffix$NoTag())\n");
        }
        else {
            printer->Print(vars, "$fieldName$ = $converter$(input.read$suffix$($fieldNumber$))\n");
        }
    }
    else {
        if (noTag) {
            printer->Print(vars, "output.write$suffix$NoTag ()\n");
        }
        else {
            printer->Print(vars, "output.write$suffix$ ($fieldNumber$, $fieldName$.ord)\n");
        }
    }
}

void FieldGenerator::generateSerializationForMessages(io::Printer * printer, bool isRead, bool noTag, bool isField) const {
    map <string, string> vars;
    vars["fieldNumber"] = std::to_string(getFieldNumber());
    vars["dollar"] = "$";
    vars["fieldName"] = simpleName;

    if (isRead) {
        // We will create some temporary variables
        // So we place following code into separate block for the sake of hygiene
        printer->Print("run {\n");
        printer->Indent();

        // read tag
        if (!noTag) {
            printer->Print(vars, "input.readTag($fieldNumber$, WireType.LENGTH_DELIMITED)\n");
        }

        // read expected size
        printer->Print(vars, "val expectedSize = input.readInt32NoTag()\n");

        // TODO: think about it, as it's not good approach - if some error occurs, we will read more bytes
        // than expectedSize from CodedInputStream. That could potentially lead to some lingering problems in wire.

        // read message itself without tag, but limiting its size to expectedSize
        printer->Print(vars,
                       "$fieldName$.mergeFromWithSize(input, expectedSize)\n");

        // check that actual size equal to expected size
        printer->Print(vars, "if (expectedSize != $fieldName$.getSizeNoTag()) { errorCode = 3; return false }\n");
        printer->Outdent();
        printer->Print("}\n");
    }
    else {
        // write tag
        printer->Print(vars, "output.writeTag($fieldNumber$, WireType.LENGTH_DELIMITED)\n");

        // write message length
        printer->Print(vars, "output.writeInt32NoTag($fieldName$.getSizeNoTag())\n");

        // write message itself without tag
        printer->Print(vars,
                       "$fieldName$.writeTo(output)\n");
    }
}

void FieldGenerator::generateSerializationForPrimitives(io::Printer * printer, bool isRead, bool noTag, bool isField) const {
    map <string, string> vars;
    vars["fieldName"] = simpleName;
    vars["suffix"] = getKotlinFunctionSuffix();
    vars["fieldNumber"] = std::to_string(getFieldNumber());
    if (isRead) {
        if (noTag) {
            printer->Print(vars, "$fieldName$ = input.read$suffix$NoTag()\n");
        }
        else {
            printer->Print(vars, "$fieldName$ = input.read$suffix$ ($fieldNumber$)");
        }
    }
    else {
        if (noTag) {
            printer->Print(vars, "output.write$suffix$NoTag ($fieldName$)\n");
        }
        else {
            printer->Print(vars, "output.write$suffix$ ($fieldNumber$, $fieldName$)\n");
        }
    }
}

void FieldGenerator::generateSerializationCode(io::Printer *printer, bool isRead, bool noTag, bool isField) const {
    map <string, string> vars;
    vars["fieldName"] = simpleName;
    vars["initValue"] = getInitValue();


    /* Try to generate syntax for serialization of repeated fields.
     * Note that it should be first check because of Google's FieldDescriptor structure */
    if (getProtoLabel() == FieldDescriptor::LABEL_REPEATED) {
        // we shouldn't write fields with default values when writing
        if (!isRead && isField) {
            printer->Print(vars, "if ($fieldName$.size > 0) {\n");
            printer->Indent();
        }

        bool isPrimitive = descriptor->type() != FieldDescriptor::TYPE_BYTES &&
                            descriptor->type() != FieldDescriptor::TYPE_MESSAGE &&
                            descriptor->type() != FieldDescriptor::TYPE_STRING;
        if (isPrimitive) {
            generateSerializationForPacked(printer, isRead, noTag, isField);
        }
        else {
            throw UnreachableStateException("Arrays of non-primitive types are not supported");
        }
    }

    else {
        // we shouldn't write fair fields with default values
        if (!isRead && isField) {
            printer->Print(vars, "if ($fieldName$ != $initValue$) {\n");
            printer->Indent();
        }

        /* Then check is current field is enum. We have to handle it separately too, because
         * we have to pass enums as Int's to CodedStreams as per protobuf-format */
        if (descriptor->type() == FieldDescriptor::TYPE_ENUM) {
            generateSerializationForEnums(printer, isRead, noTag, isField);
        }

        /* Then check for nested messages. Here we re-use writeTo method, that should be defined in
         * that message.
         * Note that readFrom/writeTo methods write message as it's top-level message, i.e. without
         * any tags. Therefore, we have to prepend tags and size manually. */
        else if (descriptor->type() == FieldDescriptor::TYPE_MESSAGE) {
            generateSerializationForMessages(printer, isRead, noTag, isField);
        }

        /* Finally, serialize trivial cases */
        else {
            generateSerializationForPrimitives(printer, isRead, noTag, isField);
        }
    }

    if (!isRead && isField) {
        printer->Outdent();
        printer->Print("}\n");
    }
}


void FieldGenerator::generateSetter(io::Printer *printer) const {
    map <string, string> vars;
    vars["camelCaseName"] = name_resolving::makeFirstLetterUpper(simpleName);
    vars["fieldName"] = simpleName;
    vars["builderName"] = enclosingClass->getBuilderFullType();
    vars["type"] = getFullType();
    printer->Print(vars,
                    "fun set$camelCaseName$(value: $type$): $builderName$ {\n");
    printer->Indent();
    printer->Print(vars,
                    "$fieldName$ = value\n"
                    "return this\n");
    printer->Outdent();
    printer->Print("}\n");
}

void FieldGenerator::generateRepeatedMethods(io::Printer * printer, bool isBuilder) const {
    map <string, string> vars;
    vars["elementType"] = getUnderlyingTypeGenerator().getSimpleType();
    vars["arg"] = "value";
    vars["fieldName"] = simpleName;
    vars["builderName"] = enclosingClass->getBuilderFullType();

    // generate indexed setter for builders
    if (isBuilder) {
        printer->Print(vars, "fun set$fieldName$ByIndex(index: Int, $arg$: $elementType$): $builderName$ {\n");
        printer->Indent();
        printer->Print(vars, "$fieldName$[index] = $arg$\n");
        printer->Print(vars, "return this\n");
        printer->Outdent();
        printer->Print("}\n");
    }
}

string FieldGenerator::getKotlinFunctionSuffix() const {
    return name_resolving::protobufTypeToKotlinFunctionSuffix(descriptor->type());
}

void FieldGenerator::generateSizeEstimationCode(io::Printer *printer, string varName, bool noTag, bool isField) const {
    map<string, string> vars;
    vars["varName"] = varName;
    vars["fieldName"] = simpleName;
    vars["fieldNumber"] = std::to_string(getFieldNumber());
    vars["initValue"] = getInitValue();

    // First of all, generate code for repeated fields
    if (getProtoLabel() == FieldDescriptor::LABEL_REPEATED) {
        printer->Print(vars, "if ($fieldName$.size != 0) {\n");
        printer->Indent();

        // We will need total byte size of array, because that size is itself a part of the message and
        // adds to total message size.
        // For the sake of hygiene, temporary variables are created in anonymous scope
        printer->Print("run {\n");
        printer->Indent();

        // Create a temporary variable that will collect array byte size
        printer->Print("var arraySize = 0\n");

        // iterate over all elements of array
        printer->Print(vars, "for (item in $fieldName$) {\n");
        printer->Indent();

        // hack: reuse generateSizeEstimationCode in the same manner as in generateSerializationCode
        FieldGenerator singleFieldGen = FieldGenerator(descriptor, enclosingClass, nameResolver);
        singleFieldGen.protoLabel = FieldDescriptor::LABEL_OPTIONAL;
        singleFieldGen.simpleName = "item";
        singleFieldGen.generateSizeEstimationCode(printer, "arraySize", noTag, /* isField = */ false);

        printer->Outdent();     // for-loop
        printer->Print("}\n");

        // now add size of array to total message size:
        printer->Print(vars,
                       "$varName$ += arraySize"); // actual array size
        printer->Print("\n");
        printer->Outdent();     // anonymous scope
        printer->Print("}\n");

        printer->Outdent();     // if-clause
        printer->Print("}\n");

        return;
    }

    // If we are generating code for fair field (not for dummy), then we should check for default value
    // Note that we can't lift that check up, because repeated fields should be treated separately
    if (isField) {
        printer->Print(vars, "if ($fieldName$ != $initValue$) {\n");
        printer->Indent();
    }

    // Then, call getSize recursively for nested messages
    // TODO: currently suboptimal repeatative calls getSize() are being made. We can optimize it later via caching calls to getSize()
    if (getProtoType() == FieldDescriptor::TYPE_MESSAGE) {
        vars["maybeNoTag"] = noTag ? "NoTag" : "";
        vars["maybeFieldNumber"] = noTag ? "" : std::to_string(getFieldNumber());
        printer->Print(vars, "$varName$ += $fieldName$.getSize$maybeNoTag$($maybeFieldNumber$)"
                "\n");
    }


    // Next, process enums as they should be casted to ints manually
    else if (getProtoType() == FieldDescriptor::TYPE_ENUM) {
        printer->Print(vars, "$varName$ += WireFormat.getEnumSize($fieldNumber$, $fieldName$.ord)\n");
    }

    // Finally, get size of all primitive types trivially via call to WireFormat in runtime
    else {
        vars["kotlinSuffix"] = getKotlinFunctionSuffix();
        printer->Print(vars, "$varName$ += WireFormat.get$kotlinSuffix$Size($fieldNumber$, $fieldName$)\n");
    }

    if (isField) {
        printer->Outdent();     // if-clause
        printer->Print("}\n");
    }
}

FieldDescriptor::Label FieldGenerator::getProtoLabel() const {
    return protoLabel;
}

FieldDescriptor::Type FieldGenerator::getProtoType() const {
    return descriptor->type();
}

int FieldGenerator::getFieldNumber() const {
    return descriptor->number();
}


string FieldGenerator::getSimpleType() const {
    if (getProtoLabel() == FieldDescriptor::LABEL_REPEATED) {
        return "MutableList <" + getUnderlyingTypeGenerator().getSimpleType() + ">";
    }
    if (getProtoType() == FieldDescriptor::TYPE_MESSAGE) {
        return descriptor->message_type()->name();
    }
    if (getProtoType() == FieldDescriptor::TYPE_ENUM) {
        return descriptor->enum_type()->name();
    }
    return name_resolving::protobufToKotlinType(descriptor->type());
}

string FieldGenerator::getFullType() const {
    if (getProtoLabel() == FieldDescriptor::LABEL_REPEATED) {
        return getUnderlyingTypeGenerator().getFullType() + "Array";
    }
    if (getProtoType() == FieldDescriptor::TYPE_MESSAGE ||
            getProtoType() == FieldDescriptor::TYPE_ENUM) {
        return nameResolver->getClassName(getSimpleType());
    }
    return name_resolving::protobufToKotlinType(getProtoType());
}

string FieldGenerator::getBuilderFullType() const {
    if (getProtoType() != FieldDescriptor::TYPE_MESSAGE) {
        throw UnreachableStateException("Error: trying to get builder name for non-message field " + simpleName);
    }
    return nameResolver->getBuilderName(getSimpleType());
}

string FieldGenerator::getBuilderSimpleType() const {
    if (getProtoType() != FieldDescriptor::TYPE_MESSAGE) {
        throw UnreachableStateException("Error: trying to get builder name for non-message field " + simpleName);
    }
    return "Builder" + getSimpleType();
}

string FieldGenerator::getEnumFromIntConverter() const {
    return getFullType() + ".fromIntTo" + getSimpleType();
}

FieldGenerator FieldGenerator::getUnderlyingTypeGenerator() const {
    if (getProtoLabel() == FieldDescriptor::LABEL_REPEATED) {
        FieldGenerator singleFieldGen = FieldGenerator(descriptor, enclosingClass, nameResolver);
        singleFieldGen.protoLabel = FieldDescriptor::LABEL_OPTIONAL;
        return singleFieldGen;
    }
    return *this;
}

string FieldGenerator::getWireType() const {
    if (descriptor->label() == FieldDescriptor::LABEL_REPEATED) {
        return "WireType.LENGTH_DELIMITED";
    }
    return name_resolving::protobufTypeToKotlinWireType(descriptor->type());
}


} // namespace kotlin
} // namespace compiler
} // namespace protobuf
} // namespace google